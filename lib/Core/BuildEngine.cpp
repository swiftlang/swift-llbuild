//===-- BuildEngine.cpp ---------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Core/BuildEngine.h"

#include "llbuild/Core/BuildDB.h"

#include "BuildEngineTrace.h"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

Task::~Task() {}

BuildEngineDelegate::~BuildEngineDelegate() {}

#pragma mark - BuildEngine Implementation

namespace {

class BuildEngineImpl {
  struct RuleInfo;
  struct TaskInfo;

  /// Reserved input IDs.
  enum: uintptr_t {
    kMustFollowInputID = ~(uintptr_t)0
  };

  BuildEngine& BuildEngine;

  BuildEngineDelegate& Delegate;

  /// The build database, if attached.
  std::unique_ptr<BuildDB> DB;

  /// The tracing implementation, if enabled.
  std::unique_ptr<BuildEngineTrace> Trace;

  /// The current build iteration, used to sequentially timestamp build results.
  uint64_t CurrentTimestamp = 0;

  /// The queue of input requests to process.
  struct TaskInputRequest {
    /// The task making the request.
    TaskInfo* TaskInfo;
    /// The rule for the input which was requested.
    RuleInfo* InputRuleInfo;
    /// The task provided input ID, for its own use in identifying the input.
    uintptr_t InputID;
  };
  std::vector<TaskInputRequest> InputRequests;

  /// The queue of rules being scanned.
  struct RuleScanRequest {
    /// The rule making the request.
    RuleInfo* RuleInfo;
    /// The input index being considered.
    unsigned InputIndex;
    /// The input being considered, if already looked up.
    ///
    /// This is used when a scan request is deferred waiting on its input to be
    /// scanned, to avoid a redundant hash lookup.
    struct RuleInfo* InputRuleInfo;
  };
  std::vector<RuleScanRequest> RuleInfosToScan;

  struct RuleScanRecord {
    /// The vector of paused input requests, waiting for the dependency scan on
    /// this rule to complete.
    std::vector<TaskInputRequest> PausedInputRequests;
    /// The vector of deferred scan requests, for rules which are waiting on
    /// this one to be scanned.
    std::vector<RuleScanRequest> DeferredScanRequests;
  };

  /// Wrapper for information specific to a single rule.
  struct RuleInfo {
    enum class StateKind {
      /// The initial rule state.
      Incomplete = 0,

      /// The rule is being scanned to determine if it needs to run.
      IsScanning,

      /// The rule needs to run, but has not yet been started.
      NeedsToRun,

      /// The rule does not need to run, but has not yet been marked as
      /// complete.
      DoesNotNeedToRun,

      /// The rule is in progress, but is waiting on additional inputs.
      InProgressWaiting,

      /// The rule is in progress, and is computing its result.
      InProgressComputing,

      /// The rule is complete, with an available result.
      ///
      /// Note that as an optimization, when the build timestamp is incremented
      /// we do not immediately reset the state, rather we do it lazily as part
      /// of \see demandRule() in conjunction with the Result::BuiltAt field.
      Complete
    };

    RuleInfo(Rule &&Rule) : Rule(Rule) {}

    Rule Rule;
    /// The state dependent record for in-progress information.
    union {
      RuleScanRecord* PendingScanRecord;
      TaskInfo* PendingTaskInfo;
    } InProgressInfo = { nullptr };
    /// The most recent rule result.
    Result Result = {};
    /// The current state of the rule.
    StateKind State = StateKind::Incomplete;

  public:
    bool isScanning() const {
      return State == StateKind::IsScanning;
    }

    bool isScanned(const BuildEngineImpl* Engine) const {
      // If the rule is marked as complete, just check that state.
      if (State == StateKind::Complete)
        return isComplete(Engine);

      // Otherwise, the rule is scanned if it has passed the scanning state.
      return int(State) > int(StateKind::IsScanning);
    }

    bool isInProgressWaiting() const {
      return State == StateKind::InProgressWaiting;
    }

    bool isInProgressComputing() const {
      return State == StateKind::InProgressComputing;
    }

    bool isInProgress() const {
      return isInProgressWaiting() || isInProgressComputing();
    }

    bool isComplete(const BuildEngineImpl* Engine) const {
      return State == StateKind::Complete &&
        Result.BuiltAt == Engine->getCurrentTimestamp();
    }

    void setComplete(const BuildEngineImpl* Engine) {
      State = StateKind::Complete;
      // Note we do not push this change to the database. This is essentially a
      // mark we maintain to allow a lazy transition to Incomplete when the
      // timestamp is incremented.
      //
      // FIXME: This is a bit dubious, and wouldn't work anymore if we moved the
      // Result to being totally managed by the database. However, it is just a
      // matter of keeping an extra timestamp outside the Result to fix.
      Result.BuiltAt = Engine->getCurrentTimestamp();
    }

    RuleScanRecord* getPendingScanRecord() {
      assert(isScanning());
      return InProgressInfo.PendingScanRecord;
    }
    void setPendingScanRecord(RuleScanRecord* Value) {
      InProgressInfo.PendingScanRecord = Value;
    }

    TaskInfo* getPendingTaskInfo() {
      assert(isInProgress());
      return InProgressInfo.PendingTaskInfo;
    }
    void setPendingTaskInfo(TaskInfo* Value) {
      assert(isInProgress());
      InProgressInfo.PendingTaskInfo = Value;
    }
  };

  // The map of registered rules.
  //
  // NOTE: We currently rely on the unordered_map behavior that ensures that
  // references to elements are not invalidated by insertion. We will need to
  // move to an alternate allocation strategy if we switch to DenseMap style
  // table.
  std::unordered_map<KeyType, RuleInfo> RuleInfos;

  /// The set of pending tasks.
  struct TaskInfo {
    TaskInfo(Task* Task) : Task(Task) {}

    std::unique_ptr<Task> Task;
    /// The list of input requests that are waiting on this task, which will be
    /// fulfilled once the task is complete.
    //
    // FIXME: Note that this structure is redundant here, as
    // (TaskInputRequest::TaskInfo == this) for all items, but we reuse the
    // existing structure for simplicity.
    std::vector<TaskInputRequest> RequestedBy;
    /// The rule that this task is computing.
    RuleInfo* ForRuleInfo = nullptr;
    /// The number of outstanding inputs that this task is waiting on to be
    /// provided.
    unsigned WaitCount = 0;
    /// The list of discovered dependencies found during execution of the task.
    std::vector<KeyType> DiscoveredDependencies;
  };
  std::unordered_map<Task*, TaskInfo> TaskInfos;

  /// The queue of tasks ready to be finalized.
  std::vector<TaskInfo*> ReadyTaskInfos;

  /// The number tasks which have been readied but not yet finished.
  unsigned NumOutstandingUnfinishedTasks = 0;

  /// The queue of tasks which are complete, accesses to this member variable
  /// must be protected via \see FinishedTaskInfosMutex.
  std::vector<TaskInfo*> FinishedTaskInfos;

  /// The mutex that protects finished task infos.
  std::mutex FinishedTaskInfosMutex;

  /// This variable is used to signal when additional work is added to the
  /// FinishedTaskInfos queue, which the engine may need to wait on.
  std::condition_variable FinishedTaskInfosCondition;

private:
  /// @name RuleScanRecord Allocation
  ///
  /// The execution of a single build may use a substantial amount of
  /// additional memory in recording the bookkeeping information used to do
  /// dependency scanning. While we could keep this information adjacent to
  /// every \see RuleInfo, that adds up to a substantial check of memory which
  /// is wasted except during dependency scanning.
  ///
  /// Instead, we allocate \see RuleScanRecord objects for each rule only as it
  /// is being scanned, and use a custom allocator for the objects to try and
  /// make this efficient. Currently we use a bounded free-list backed by a slab
  /// allocator.
  ///
  /// Note that this still has a fairly large impact on dependency scanning
  /// performance, in the worst case a deep linear graph takes ~50% longer to
  /// scan, but it also provides an overall 15-20% memory savings on resident
  /// engine size.
  ///
  /// @{

  // FIXME: This should be abstracted into a helper class.

  /// A free-list of RuleScanRecord objects.
  std::vector<RuleScanRecord*> FreeRuleScanRecords;
  /// The maximum number of free-list items to keep.
  const size_t MaximumFreeRuleScanRecords = 8096;
  /// The list of blocks (of size \see NumScanRecordsPerBlock) we have
  /// allocated.
  std::vector<RuleScanRecord*> RuleScanRecordBlocks;
  /// The number of records to allocate per block.
  const size_t NumScanRecordsPerBlock = 4096;
  /// The buffer positions of the current block.
  RuleScanRecord* CurrentBlockPos = nullptr, * CurrentBlockEnd = nullptr;

  RuleScanRecord* newRuleScanRecord() {
    // If we have an item on the free list, return it.
    if (!FreeRuleScanRecords.empty()) {
      auto Result = FreeRuleScanRecords.back();
      FreeRuleScanRecords.pop_back();
      return Result;
    }

    // If we are at the end of a block, allocate a new one.
    if (CurrentBlockPos == CurrentBlockEnd) {
      CurrentBlockPos = new RuleScanRecord[NumScanRecordsPerBlock];
      RuleScanRecordBlocks.push_back(CurrentBlockPos);
      CurrentBlockEnd = CurrentBlockPos + NumScanRecordsPerBlock;
    }
    return CurrentBlockPos++;
  }

  void freeRuleScanRecord(RuleScanRecord* ScanRecord) {
    if (FreeRuleScanRecords.size() < MaximumFreeRuleScanRecords) {
      ScanRecord->PausedInputRequests.clear();
      ScanRecord->DeferredScanRequests.clear();
      FreeRuleScanRecords.push_back(ScanRecord);
    }
  }

  /// @}

  /// @name Build Execution
  /// @{

  /// Request the scanning of the given rule to determine if it needs to run in
  /// the current environment.
  ///
  /// \returns True if the rule is already scanned, otherwise the rule will be
  /// enqueued for processing.
  bool scanRule(RuleInfo& RuleInfo) {
    // If the rule is already scanned, we are done.
    if (RuleInfo.isScanned(this))
      return true;

    // If the rule is being scanned, we don't need to do anything.
    if (RuleInfo.isScanning())
      return false;

    // Otherwise, start scanning the rule.
    if (Trace)
      Trace->checkingRuleNeedsToRun(&RuleInfo.Rule);

    // If the rule has never been run, it needs to run.
    if (RuleInfo.Result.BuiltAt == 0) {
      if (Trace)
        Trace->ruleNeedsToRunBecauseNeverBuilt(&RuleInfo.Rule);
      RuleInfo.State = RuleInfo::StateKind::NeedsToRun;
      return true;
    }

    // If the rule indicates its computed value is out of date, it needs to run.
    if (RuleInfo.Rule.IsResultValid &&
        !RuleInfo.Rule.IsResultValid(RuleInfo.Rule, RuleInfo.Result.Value)) {
      if (Trace)
        Trace->ruleNeedsToRunBecauseInvalidValue(&RuleInfo.Rule);
      RuleInfo.State = RuleInfo::StateKind::NeedsToRun;
      return true;
    }

    // If the rule has no dependencies, then it is ready to run.
    if (RuleInfo.Result.Dependencies.empty()) {
      if (Trace)
        Trace->ruleDoesNotNeedToRun(&RuleInfo.Rule);
      RuleInfo.State = RuleInfo::StateKind::DoesNotNeedToRun;
      return true;
    }

    // Otherwise, we need to do a recursive scan of the inputs so enqueue this
    // rule for scanning.
    //
    // We could also take an approach where we enqueue each of the individual
    // inputs, but in my experiments with that approach it has always performed
    // significantly worse.
    if (Trace)
      Trace->ruleScheduledForScanning(&RuleInfo.Rule);
    RuleInfo.State = RuleInfo::StateKind::IsScanning;
    RuleInfo.setPendingScanRecord(newRuleScanRecord());
    RuleInfosToScan.push_back({ &RuleInfo, /*InputIndex=*/0, nullptr });

    return false;
  }

  /// Request the construction of the key specified by the given rule.
  ///
  /// \returns True if the rule is already available, otherwise the rule will be
  /// enqueued for processing.
  bool demandRule(RuleInfo& RuleInfo) {
    // The rule must have already been scanned.
    assert(RuleInfo.isScanned(this));

    // If the rule is complete, we are done.
    if (RuleInfo.isComplete(this))
      return true;

    // If the rule is in progress, we don't need to do anything.
    if (RuleInfo.isInProgress())
      return false;

    // If the rule isn't marked complete, but doesn't need to actually run, then
    // just update it.
    if (RuleInfo.State == RuleInfo::StateKind::DoesNotNeedToRun) {
      RuleInfo.setComplete(this);
      return true;
    }

    // Otherwise, we actually need to initiate the processing of this rule.
    assert(RuleInfo.State == RuleInfo::StateKind::NeedsToRun);

    // Create the task for this rule.
    Task* Task = RuleInfo.Rule.Action(BuildEngine);

    // Find the task info for this task.
    auto it = TaskInfos.find(Task);
    assert(it != TaskInfos.end() &&
           "rule action returned an unregistered task");
    TaskInfo* TaskInfo = &it->second;
    TaskInfo->ForRuleInfo = &RuleInfo;

    if (Trace)
      Trace->createdTaskForRule(TaskInfo->Task.get(), &RuleInfo.Rule);

    // Transition the rule state.
    RuleInfo.State = RuleInfo::StateKind::InProgressWaiting;
    RuleInfo.setPendingTaskInfo(TaskInfo);

    // Reset the Rule result state. The only field we must reset here is the
    // Dependencies, which we just append to during processing, but we reset the
    // others to ensure no one ever inadvertently uses them during an invalid
    // state.
    //
    // FIXME: Eliminate the clearing of Value here, which might have a
    // performance cost.
    RuleInfo.Result.Value = ValueType();
    RuleInfo.Result.BuiltAt = 0;
    RuleInfo.Result.ComputedAt = 0;
    RuleInfo.Result.Dependencies.clear();

    // Inform the task it should start.
    Task->start(BuildEngine);

    // If this task has no waiters, schedule it immediately for finalization.
    if (!TaskInfo->WaitCount) {
      ReadyTaskInfos.push_back(TaskInfo);
    }

    return false;
  }

  /// Process an individual scan request.
  ///
  /// This will process all of the inputs required by the requesting rule, in
  /// order, unless the scan needs to be deferred waiting for an input.
  void processRuleScanRequest(RuleScanRequest Request) {
    auto& RuleInfo = *Request.RuleInfo;

    assert(RuleInfo.isScanning());

    // Process each of the remaining inputs.
    do {
      // Look up the input rule info, if not yet cached.
      if (!Request.InputRuleInfo) {
        const auto& InputKey = RuleInfo.Result.Dependencies[Request.InputIndex];
        Request.InputRuleInfo = &getRuleInfoForKey(InputKey);
      }
      auto& InputRuleInfo = *Request.InputRuleInfo;

      // Scan the input.
      bool IsScanned = scanRule(InputRuleInfo);

      // If the input isn't scanned yet, enqueue this input scan request.
      if (!IsScanned) {
        assert(InputRuleInfo.isScanning());
        if (Trace)
          Trace->ruleScanningDeferredOnInput(&RuleInfo.Rule,
                                             &InputRuleInfo.Rule);
        InputRuleInfo.getPendingScanRecord()
          ->DeferredScanRequests.push_back(Request);
        return;
      }

      if (Trace)
        Trace->ruleScanningNextInput(&RuleInfo.Rule, &InputRuleInfo.Rule);

      // Demand the input.
      bool IsAvailable = demandRule(InputRuleInfo);

      // If the input wasn't already available, it needs to run.
      if (!IsAvailable) {
        // FIXME: This is just wrong, just because we haven't run the task yet
        // doesn't necessarily mean that this rule needs to run, if running
        // the task results in an output that hasn't changed (and so
        // ComputedAt isn't updated). This case doesn't come up until we
        // support BuiltAt != ComputedAt, though.
        if (Trace)
          Trace->ruleNeedsToRunBecauseInputUnavailable(
            &RuleInfo.Rule, &InputRuleInfo.Rule);
        finishScanRequest(RuleInfo, RuleInfo::StateKind::NeedsToRun);
        return;
      }

      // If the input has been computed since the last time this rule was
      // built, it needs to run.
      if (RuleInfo.Result.BuiltAt < InputRuleInfo.Result.ComputedAt) {
        if (Trace)
          Trace->ruleNeedsToRunBecauseInputRebuilt(
            &RuleInfo.Rule, &InputRuleInfo.Rule);
        finishScanRequest(RuleInfo, RuleInfo::StateKind::NeedsToRun);
        return;
      }

      // Otherwise, increment the scan index.
      ++Request.InputIndex;
      Request.InputRuleInfo = nullptr;
    } while (Request.InputIndex != RuleInfo.Result.Dependencies.size());

    // If we reached the end of the inputs, the rule does not need to run.
    if (Trace)
      Trace->ruleDoesNotNeedToRun(&RuleInfo.Rule);
    finishScanRequest(RuleInfo, RuleInfo::StateKind::DoesNotNeedToRun);
  }

  void finishScanRequest(RuleInfo& InputRuleInfo,
                         RuleInfo::StateKind NewState) {
    assert(InputRuleInfo.isScanning());
    auto ScanRecord = InputRuleInfo.getPendingScanRecord();

    // Wake up all of the pending scan requests.
    for (const auto& Request: ScanRecord->DeferredScanRequests) {
      RuleInfosToScan.push_back(Request);
    }

    // Wake up all of the input requests on this rule.
    for (const auto& Request: ScanRecord->PausedInputRequests) {
      InputRequests.push_back(Request);
    }

    // Update the rule state.
    freeRuleScanRecord(ScanRecord);
    InputRuleInfo.setPendingScanRecord(nullptr);
    InputRuleInfo.State = NewState;
  }

  /// Decrement the task's wait count, and move it to the ready queue if
  /// necessary.
  void decrementTaskWaitCount(TaskInfo* TaskInfo) {
    --TaskInfo->WaitCount;
    if (Trace)
      Trace->updatedTaskWaitCount(TaskInfo->Task.get(), TaskInfo->WaitCount);
    if (TaskInfo->WaitCount == 0) {
      if (Trace)
        Trace->unblockedTask(TaskInfo->Task.get());
      ReadyTaskInfos.push_back(TaskInfo);
    }
  }

  /// Execute all of the work pending in the engine queues until they are empty.
  void executeTasks() {
    std::vector<TaskInputRequest> FinishedInputRequests;

    // Process requests as long as we have work to do.
    while (true) {
      bool DidWork = false;

      // Process all of the pending rule scan requests.
      //
      // FIXME: We don't want to process all of these requests, this amounts to
      // doing all of the dependency scanning up-front.
      while (!RuleInfosToScan.empty()) {
        DidWork = true;

        auto Request = RuleInfosToScan.back();
        RuleInfosToScan.pop_back();

        processRuleScanRequest(Request);
      }

      // Process all of the pending input requests.
      while (!InputRequests.empty()) {
        DidWork = true;

        auto Request = InputRequests.back();
        InputRequests.pop_back();

        if (Trace) {
          if (Request.TaskInfo) {
            Trace->handlingTaskInputRequest(Request.TaskInfo->Task.get(),
                                            &Request.InputRuleInfo->Rule);
          } else {
            Trace->handlingBuildInputRequest(&Request.InputRuleInfo->Rule);
          }
        }

        // Request the input rule be scanned.
        bool IsScanned = scanRule(*Request.InputRuleInfo);

        // If the rule is not yet scanned, suspend this input request.
        if (!IsScanned) {
          assert(Request.InputRuleInfo->isScanning());
          if (Trace)
            Trace->pausedInputRequestForRuleScan(
              &Request.InputRuleInfo->Rule);
          Request.InputRuleInfo->getPendingScanRecord()
            ->PausedInputRequests.push_back(Request);
          continue;
        }

        // Request the input rule be computed.
        bool IsAvailable = demandRule(*Request.InputRuleInfo);

        // If this is a dummy input request, we are done.
        if (!Request.TaskInfo)
          continue;

        // If the rule is already available, enqueue the finalize request.
        if (IsAvailable) {
          if (Trace)
            Trace->readyingTaskInputRequest(Request.TaskInfo->Task.get(),
                                            &Request.InputRuleInfo->Rule);
          FinishedInputRequests.push_back(Request);
        } else {
          // Otherwise, record the pending input request.
          assert(Request.InputRuleInfo->getPendingTaskInfo());
          if (Trace)
            Trace->addedRulePendingTask(&Request.InputRuleInfo->Rule,
                                        Request.TaskInfo->Task.get());
          Request.InputRuleInfo->getPendingTaskInfo()->RequestedBy.push_back(
            Request);
        }
      }

      // Process all of the finished inputs.
      while (!FinishedInputRequests.empty()) {
        DidWork = true;

        auto Request = FinishedInputRequests.back();
        FinishedInputRequests.pop_back();

        if (Trace)
          Trace->completedTaskInputRequest(Request.TaskInfo->Task.get(),
                                           &Request.InputRuleInfo->Rule);

        // If this is a must follow input, we simply decrement the task wait
        // count.
        //
        // This works because must follow inputs do not need to be recorded or
        // scanned -- they are only relevant if the task is executing, in which
        // case it is responsible for having supplied the request.
        if (Request.InputID == kMustFollowInputID) {
          decrementTaskWaitCount(Request.TaskInfo);
          continue;
        }

        // Otherwise, we are processing a regular input dependency.

        // Update the recorded dependencies of this task.
        //
        // FIXME: This is very performance critical and should be highly
        // optimized. By itself, this addition added about 25% user time to the
        // "ack 3 16" experiment.
        //
        // FIXME: Think about where the best place to record this is. Our
        // options are:
        // * Record at the time it is requested.
        // * Record at the time it is popped off the input request queue.
        // * Record at the time the input is supplied (here).
        Request.TaskInfo->ForRuleInfo->Result.Dependencies.push_back(
          Request.InputRuleInfo->Rule.Key);

        // Provide the requesting task with the input.
        //
        // FIXME: Should we provide the input key here? We have it available
        // cheaply.
        assert(Request.InputRuleInfo->isComplete(this));
        Request.TaskInfo->Task->provideValue(
          BuildEngine, Request.InputID, Request.InputRuleInfo->Result.Value);

        // Decrement the wait count, and move to finish queue if necessary.
        decrementTaskWaitCount(Request.TaskInfo);
      }

      // Process all of the ready to run tasks.
      while (!ReadyTaskInfos.empty()) {
        DidWork = true;

        TaskInfo* TaskInfo = ReadyTaskInfos.back();
        ReadyTaskInfos.pop_back();

        RuleInfo* RuleInfo = TaskInfo->ForRuleInfo;
        assert(TaskInfo == RuleInfo->getPendingTaskInfo());

        if (Trace)
            Trace->readiedTask(TaskInfo->Task.get(), &RuleInfo->Rule);

        // Transition the rule state.
        assert(RuleInfo->isInProgressWaiting());
        RuleInfo->State = RuleInfo::StateKind::InProgressComputing;

        // Inform the task its inputs are ready and it should finish.
        //
        // FIXME: We need to track this state, and generate an error if this
        // task ever requests additional inputs.
        TaskInfo->Task->inputsAvailable(BuildEngine);

        // Increment our count of outstanding tasks.
        ++NumOutstandingUnfinishedTasks;
      }

      // Process all of the finished tasks.
      while (true) {
        // Try to take a task from the finished queue.
        TaskInfo* TaskInfo = nullptr;
        {
          std::lock_guard<std::mutex> Guard(FinishedTaskInfosMutex);
          if (!FinishedTaskInfos.empty()) {
            TaskInfo = FinishedTaskInfos.back();
            FinishedTaskInfos.pop_back();
          }
        }
        if (!TaskInfo)
          break;

        DidWork = true;

        RuleInfo* RuleInfo = TaskInfo->ForRuleInfo;
        assert(TaskInfo == RuleInfo->getPendingTaskInfo());

        if (Trace) {
            Trace->finishedTask(TaskInfo->Task.get(), &RuleInfo->Rule,
                                /*WasChanged=*/true);
        }

        // Transition the rule state.
        assert(RuleInfo->State == RuleInfo::StateKind::InProgressComputing);
        RuleInfo->setPendingTaskInfo(nullptr);
        RuleInfo->State = RuleInfo::StateKind::Complete;

        // Complete the rule (the value itself is stored in the taskIsFinished
        // call).
        RuleInfo->Result.ComputedAt = CurrentTimestamp;
        RuleInfo->Result.BuiltAt = CurrentTimestamp;

        // Add all of the task's discovered dependencies.
        //
        // FIXME: We could audit these dependencies at this point to verify that
        // they are not keys for rules which have not been run, which would
        // indicate an underspecified build (e.g., a generated header).
        RuleInfo->Result.Dependencies.insert(
          RuleInfo->Result.Dependencies.begin(),
          TaskInfo->DiscoveredDependencies.begin(),
          TaskInfo->DiscoveredDependencies.end());

        // Push back dummy input requests for any discovered dependencies, which
        // must be at least built in order to be brought up-to-date.
        //
        // FIXME: The need to do this makes it questionable that we use this
        // approach for discovered dependencies instead of just providing
        // support for taskNeedsInput() even after the task has started
        // computing and from parallel contexts.
        for (const auto& InputKey: TaskInfo->DiscoveredDependencies) {
          InputRequests.push_back({ nullptr, &getRuleInfoForKey(InputKey) });
        }

        // Update the database record, if attached.
        if (DB)
            DB->setRuleResult(RuleInfo->Rule, RuleInfo->Result);

        // Push all pending input requests onto the work queue.
        if (Trace) {
          for (auto& Request: TaskInfo->RequestedBy) {
            Trace->readyingTaskInputRequest(Request.TaskInfo->Task.get(),
                                            &Request.InputRuleInfo->Rule);
          }
        }
        FinishedInputRequests.insert(FinishedInputRequests.end(),
                                     TaskInfo->RequestedBy.begin(),
                                     TaskInfo->RequestedBy.end());

        // Decrement our count of outstanding tasks.
        --NumOutstandingUnfinishedTasks;

        // Delete the pending task.
        auto it = TaskInfos.find(TaskInfo->Task.get());
        assert(it != TaskInfos.end());
        TaskInfos.erase(it);
      }

      // If we haven't done any other work at this point but we have pending
      // tasks, we need to wait for a task to complete.
      if (!DidWork && NumOutstandingUnfinishedTasks != 0) {
        // Wait for our condition variable.
        std::unique_lock<std::mutex> Lock(FinishedTaskInfosMutex);

        // Ensure we still don't have enqueued operations under the protection
        // of the mutex, if one has been added then we may have already missed
        // the condition notification and cannot safely wait.
        if (FinishedTaskInfos.empty()) {
            FinishedTaskInfosCondition.wait(Lock);
        }

        DidWork = true;
      }

      // If we didn't do any work, we are done.
      if (!DidWork)
        break;
    }

    // FIXME: If there was no work to do, but we still have running tasks, then
    // we have found a cycle and are deadlocked.
  }

public:
  BuildEngineImpl(class BuildEngine& BuildEngine,
                  BuildEngineDelegate& Delegate)
    : BuildEngine(BuildEngine), Delegate(Delegate) {}

  ~BuildEngineImpl() {
    // If tracing is enabled, close it.
    if (Trace) {
      std::string Error;
      Trace->close(&Error);
    }
  }

  RuleInfo& getRuleInfoForKey(const KeyType& Key) {
    // Check if we have already found the rule.
    auto it = RuleInfos.find(Key);
    if (it != RuleInfos.end())
      return it->second;

    // Otherwise, request it from the delegate and add it.
    return addRule(Delegate.lookupRule(Key));
  }

  /// @name Rule Definition
  /// @{

  RuleInfo& addRule(Rule &&Rule) {
    auto Result = RuleInfos.emplace(Rule.Key, RuleInfo(std::move(Rule)));
    if (!Result.second) {
      // FIXME: Error handling.
      std::cerr << "error: attempt to register duplicate rule \""
                << Rule.Key << "\"\n";
      exit(1);
    }

    // If we have a database attached, retrieve any stored result.
    //
    // FIXME: Investigate retrieving this result lazily. If the DB is
    // particularly efficient, it may be best to retrieve this only when we need
    // it and never duplicate it.
    RuleInfo& RuleInfo = Result.first->second;
    if (DB) {
      DB->lookupRuleResult(RuleInfo.Rule, &RuleInfo.Result);
    }

    return RuleInfo;
  }

  /// @}

  /// @name Client API
  /// @{

  const ValueType& build(const KeyType& Key) {
    if (DB)
      DB->buildStarted();

    // Increment our running iteration count.
    //
    // At this point, we should conceptually mark each complete rule as
    // incomplete. However, instead of doing all that work immediately, we
    // perform it lazily by reusing the Result::BuiltAt field for each rule as
    // an additional mark. When a rule is demanded, if its BuiltAt index isn't
    // up-to-date then we lazily reset it to be Incomplete, \see demandRule()
    // and \see RuleInfo::isComplete().
    ++CurrentTimestamp;

    // Find the rule.
    auto& RuleInfo = getRuleInfoForKey(Key);

    if (Trace)
      Trace->buildStarted();

    // Push a dummy input request for this rule.
    InputRequests.push_back({ nullptr, &RuleInfo });

    // Run the build engine, to process any necessary tasks.
    executeTasks();

    // Update the build database, if attached.
    //
    // FIXME: Is it correct to do this here, or earlier?
    if (DB) {
      DB->setCurrentIteration(CurrentTimestamp);
      DB->buildComplete();
    }

    if (Trace)
      Trace->buildEnded();

    // Clear the rule scan free-lists.
    //
    // FIXME: Introduce a per-build context object to hold this.
    for (auto Block: RuleScanRecordBlocks)
      delete[] Block;
    CurrentBlockPos = CurrentBlockEnd = nullptr;
    FreeRuleScanRecords.clear();
    RuleScanRecordBlocks.clear();

    // The task queue should be empty and the rule complete.
    assert(TaskInfos.empty() && RuleInfo.isComplete(this));
    return RuleInfo.Result.Value;
  }

  void attachDB(std::unique_ptr<BuildDB> Database) {
    assert(!DB && "invalid attachDB() call");
    assert(CurrentTimestamp == 0 && "invalid attachDB() call");
    assert(RuleInfos.empty() && "invalid attachDB() call");
    DB = std::move(Database);

    // Load our initial state from the database.
    CurrentTimestamp = DB->getCurrentIteration();
  }

  bool enableTracing(const std::string& Filename, std::string* Error_Out) {
    std::unique_ptr<BuildEngineTrace> Trace(new BuildEngineTrace());

    if (!Trace->open(Filename, Error_Out))
      return false;

    this->Trace = std::move(Trace);
    return true;
  }

  /// Dump the build state to a file in Graphviz DOT format.
  void dumpGraphToFile(const std::string &Path) {
    FILE* FP = ::fopen(Path.c_str(), "w");
    if (!FP) {
      // FIXME: Error handling.
      std::cerr << "error: unable to open graph output path \""
                << Path << "\"\n";
      exit(1);
    }

    // Write the graph header.
    fprintf(FP, "digraph llbuild {\n");
    fprintf(FP, "rankdir=\"LR\"\n");
    fprintf(FP, "node [fontsize=10, shape=box, height=0.25]\n");
    fprintf(FP, "edge [fontsize=10]\n");
    fprintf(FP, "\n");

    // Create a canonical node ordering.
    std::vector<RuleInfo*> OrderedRuleInfos;
    for (auto& Entry: RuleInfos)
      OrderedRuleInfos.push_back(&Entry.second);
    std::sort(OrderedRuleInfos.begin(), OrderedRuleInfos.end(),
              [] (RuleInfo* a, RuleInfo* b) {
        return a->Rule.Key < b->Rule.Key;
      });

    // Write out all of the rules.
    for (auto RuleInfo: OrderedRuleInfos) {
      fprintf(FP, "\"%s\"\n", RuleInfo->Rule.Key.c_str());
      for (auto& Input: RuleInfo->Result.Dependencies) {
        fprintf(FP, "\"%s\" -> \"%s\"\n", RuleInfo->Rule.Key.c_str(),
                Input.c_str());
      }
      fprintf(FP, "\n");
    }

    // Write the footer and close.
    fprintf(FP, "}\n");
    fclose(FP);
  }

  void addTaskInputRequest(Task* Task, const KeyType& Key, uintptr_t InputID) {
    auto taskinfo_it = TaskInfos.find(Task);
    assert(taskinfo_it != TaskInfos.end() &&
           "cannot request inputs for an unknown task");
    TaskInfo* TaskInfo = &taskinfo_it->second;

    // Validate that the task is in a valid state to request inputs.
    if (!TaskInfo->ForRuleInfo->isInProgressWaiting()) {
      // FIXME: Error handling.
      abort();
    }

    // Lookup the rule for this task.
    RuleInfo* RuleInfo = &getRuleInfoForKey(Key);
    
    InputRequests.push_back({ TaskInfo, RuleInfo, InputID });
    TaskInfo->WaitCount++;
  }

  /// @}

  /// @name Task Management Client APIs
  /// @{

  Task* registerTask(Task* Task) {
    auto Result = TaskInfos.emplace(Task, TaskInfo(Task));
    assert(Result.second && "task already registered");
    (void)Result;
    return Task;
  }

  void taskNeedsInput(Task* Task, const KeyType& Key, uintptr_t InputID) {
    // Validate the InputID.
    if (InputID > BuildEngine::kMaximumInputID) {
      // FIXME: Error handling.
      std::cerr << "error: attempt to use reserved input ID\n";
      exit(1);
    }

    addTaskInputRequest(Task, Key, InputID);
  }

  void taskMustFollow(Task* Task, const KeyType& Key) {
    addTaskInputRequest(Task, Key, kMustFollowInputID);
  }

  void taskDiscoveredDependency(Task* Task, const KeyType& Key) {
    // Find the task info.
    //
    // FIXME: This is not safe.
    auto taskinfo_it = TaskInfos.find(Task);
    assert(taskinfo_it != TaskInfos.end() &&
           "cannot request inputs for an unknown task");
    TaskInfo* TaskInfo = &taskinfo_it->second;

    if (!TaskInfo->ForRuleInfo->isInProgressComputing()) {
      // FIXME: Error handling.
      std::cerr << "error: invalid state for adding discovered dependency\n";
      exit(1);
    }

    TaskInfo->DiscoveredDependencies.push_back(Key);
  }

  void taskIsComplete(Task* Task, ValueType&& Value) {
    // FIXME: We should flag the task to ensure this is only called once, and
    // that no other API calls are made once complete.

    // FIXME: This is not safe.
    auto taskinfo_it = TaskInfos.find(Task);
    assert(taskinfo_it != TaskInfos.end() &&
           "cannot request inputs for an unknown task");
    TaskInfo* TaskInfo = &taskinfo_it->second;

    if (!TaskInfo->ForRuleInfo->isInProgressComputing()) {
      // FIXME: Error handling.
      std::cerr << "error: invalid state for marking task complete\n";
      exit(1);
    }

    RuleInfo *RuleInfo = TaskInfo->ForRuleInfo;
    assert(TaskInfo == RuleInfo->getPendingTaskInfo());

    // Update the stored result value, and enqueue the finished task processing.
    RuleInfo->Result.Value = std::move(Value);

    // Enqueue the finished task.
    {
      std::lock_guard<std::mutex> Guard(FinishedTaskInfosMutex);
      FinishedTaskInfos.push_back(TaskInfo);
    }

    // Notify the engine to wake up, if necessary.
    FinishedTaskInfosCondition.notify_one();
  }

  /// @}

  /// @name Internal APIs
  /// @{

  uint64_t getCurrentTimestamp() const { return CurrentTimestamp; }

  /// @}
};

}

#pragma mark - BuildEngine

BuildEngine::BuildEngine(BuildEngineDelegate& Delegate)
  : Impl(new BuildEngineImpl(*this, Delegate)) 
{
}

BuildEngine::~BuildEngine() {
  delete static_cast<BuildEngineImpl*>(Impl);
}

void BuildEngine::addRule(Rule &&Rule) {
  static_cast<BuildEngineImpl*>(Impl)->addRule(std::move(Rule));
}

const ValueType& BuildEngine::build(const KeyType& Key) {
  return static_cast<BuildEngineImpl*>(Impl)->build(Key);
}

void BuildEngine::dumpGraphToFile(const std::string& Path) {
  static_cast<BuildEngineImpl*>(Impl)->dumpGraphToFile(Path);
}

void BuildEngine::attachDB(std::unique_ptr<BuildDB> Database) {
  static_cast<BuildEngineImpl*>(Impl)->attachDB(std::move(Database));
}

bool BuildEngine::enableTracing(const std::string& Path,
                                std::string* Error_Out) {
  return static_cast<BuildEngineImpl*>(Impl)->enableTracing(Path, Error_Out);
}

Task* BuildEngine::registerTask(Task* Task) {
  return static_cast<BuildEngineImpl*>(Impl)->registerTask(Task);
}

void BuildEngine::taskNeedsInput(Task* Task, const KeyType& Key,
                                 uintptr_t InputID) {
  static_cast<BuildEngineImpl*>(Impl)->taskNeedsInput(Task, Key, InputID);
}

void BuildEngine::taskDiscoveredDependency(Task* Task, const KeyType& Key) {
  static_cast<BuildEngineImpl*>(Impl)->taskDiscoveredDependency(Task, Key);
}

void BuildEngine::taskMustFollow(Task* Task, const KeyType& Key) {
  static_cast<BuildEngineImpl*>(Impl)->taskMustFollow(Task, Key);
}

void BuildEngine::taskIsComplete(Task* Task, ValueType&& Value) {
  static_cast<BuildEngineImpl*>(Impl)->taskIsComplete(Task, std::move(Value));
}
