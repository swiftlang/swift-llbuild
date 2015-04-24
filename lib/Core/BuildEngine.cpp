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
#include <unordered_set>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

Task::~Task() {}

BuildEngineDelegate::~BuildEngineDelegate() {}

#pragma mark - BuildEngine implementation

namespace {

class BuildEngineImpl {
  struct RuleInfo;
  struct TaskInfo;

  /// Reserved input IDs.
  enum: uintptr_t {
    kMustFollowInputID = ~(uintptr_t)0
  };

  BuildEngine& buildEngine;

  BuildEngineDelegate& delegate;

  /// The build database, if attached.
  std::unique_ptr<BuildDB> db;

  /// The tracing implementation, if enabled.
  std::unique_ptr<BuildEngineTrace> trace;

  /// The current build iteration, used to sequentially timestamp build results.
  uint64_t currentTimestamp = 0;

  /// The queue of input requests to process.
  struct TaskInputRequest {
    /// The task making the request.
    TaskInfo* taskInfo;
    /// The rule for the input which was requested.
    RuleInfo* inputRuleInfo;
    /// The task provided input ID, for its own use in identifying the input.
    uintptr_t inputID;
  };
  std::vector<TaskInputRequest> inputRequests;

  /// The queue of rules being scanned.
  struct RuleScanRequest {
    /// The rule making the request.
    RuleInfo* ruleInfo;
    /// The input index being considered.
    unsigned inputIndex;
    /// The input being considered, if already looked up.
    ///
    /// This is used when a scan request is deferred waiting on its input to be
    /// scanned, to avoid a redundant hash lookup.
    struct RuleInfo* inputRuleInfo;
  };
  std::vector<RuleScanRequest> ruleInfosToScan;

  struct RuleScanRecord {
    /// The vector of paused input requests, waiting for the dependency scan on
    /// this rule to complete.
    std::vector<TaskInputRequest> pausedInputRequests;
    /// The vector of deferred scan requests, for rules which are waiting on
    /// this one to be scanned.
    std::vector<RuleScanRequest> deferredScanRequests;
    /// The total number of outstanding deferred scan requests for the rule
    /// scanned with this record.
    unsigned scanWaitCount = 0;
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
      /// of \see demandRule() in conjunction with the Result::builtAt field.
      Complete
    };

    RuleInfo(Rule&& rule) : rule(rule) {}

    Rule rule;
    /// The state dependent record for in-progress information.
    union {
      RuleScanRecord* pendingScanRecord;
      TaskInfo* pendingTaskInfo;
    } inProgressInfo = { nullptr };
    /// The most recent rule result.
    Result result = {};
    /// The current state of the rule.
    StateKind state = StateKind::Incomplete;

  public:
    bool isScanning() const {
      return state == StateKind::IsScanning;
    }

    bool isScanned(const BuildEngineImpl* engine) const {
      // If the rule is marked as complete, just check that state.
      if (state == StateKind::Complete)
        return isComplete(engine);

      // Otherwise, the rule is scanned if it has passed the scanning state.
      return int(state) > int(StateKind::IsScanning);
    }

    bool isInProgressWaiting() const {
      return state == StateKind::InProgressWaiting;
    }

    bool isInProgressComputing() const {
      return state == StateKind::InProgressComputing;
    }

    bool isInProgress() const {
      return isInProgressWaiting() || isInProgressComputing();
    }

    bool isComplete(const BuildEngineImpl* engine) const {
      return state == StateKind::Complete &&
        result.builtAt == engine->getCurrentTimestamp();
    }

    void setComplete(const BuildEngineImpl* engine) {
      state = StateKind::Complete;
      // Note we do not push this change to the database. This is essentially a
      // mark we maintain to allow a lazy transition to Incomplete when the
      // timestamp is incremented.
      //
      // FIXME: This is a bit dubious, and wouldn't work anymore if we moved the
      // Result to being totally managed by the database. However, it is just a
      // matter of keeping an extra timestamp outside the Result to fix.
      result.builtAt = engine->getCurrentTimestamp();
    }

    RuleScanRecord* getPendingScanRecord() {
      assert(isScanning());
      return inProgressInfo.pendingScanRecord;
    }
    void setPendingScanRecord(RuleScanRecord* value) {
      inProgressInfo.pendingScanRecord = value;
    }

    TaskInfo* getPendingTaskInfo() {
      assert(isInProgress());
      return inProgressInfo.pendingTaskInfo;
    }
    void setPendingTaskInfo(TaskInfo* value) {
      assert(isInProgress());
      inProgressInfo.pendingTaskInfo = value;
    }
  };

  // The map of registered rules.
  //
  // NOTE: We currently rely on the unordered_map behavior that ensures that
  // references to elements are not invalidated by insertion. We will need to
  // move to an alternate allocation strategy if we switch to DenseMap style
  // table.
  std::unordered_map<KeyType, RuleInfo> ruleInfos;

  /// The set of pending tasks.
  struct TaskInfo {
    TaskInfo(Task* task) : task(task) {}

    std::unique_ptr<Task> task;
    /// The list of input requests that are waiting on this task, which will be
    /// fulfilled once the task is complete.
    //
    // FIXME: Note that this structure is redundant here, as
    // (TaskInputRequest::InputTaskInfo == this) for all items, but we reuse the
    // existing structure for simplicity.
    std::vector<TaskInputRequest> requestedBy;
    /// The vector of deferred scan requests, for rules which are waiting on
    /// this one to be scanned.
    //
    // FIXME: As above, this structure has redundancy in it.
    std::vector<RuleScanRequest> deferredScanRequests;
    /// The rule that this task is computing.
    RuleInfo* forRuleInfo = nullptr;
    /// The number of outstanding inputs that this task is waiting on to be
    /// provided.
    unsigned waitCount = 0;
    /// The list of discovered dependencies found during execution of the task.
    std::vector<KeyType> discoveredDependencies;

#ifndef NDEBUG
    void dump() const {
      fprintf(stderr,
              "<TaskInfo:%p: task:%p, for-rule:\"%s\", wait-count:%d>\n",
              this, task.get(),
              forRuleInfo ? forRuleInfo->rule.key.c_str() : "(null)",
              waitCount);
      for (const auto& request: requestedBy) {
        fprintf(stderr, "  requested by: %s\n",
                request.taskInfo->forRuleInfo->rule.key.c_str());
      }
    }
#endif    
  };
  std::unordered_map<Task*, TaskInfo> taskInfos;

  /// The queue of tasks ready to be finalized.
  std::vector<TaskInfo*> readyTaskInfos;

  /// The number tasks which have been readied but not yet finished.
  unsigned numOutstandingUnfinishedTasks = 0;

  /// The queue of tasks which are complete, accesses to this member variable
  /// must be protected via \see FinishedTaskInfosMutex.
  std::vector<TaskInfo*> finishedTaskInfos;

  /// The mutex that protects finished task infos.
  std::mutex finishedTaskInfosMutex;

  /// This variable is used to signal when additional work is added to the
  /// FinishedTaskInfos queue, which the engine may need to wait on.
  std::condition_variable finishedTaskInfosCondition;

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
  std::vector<RuleScanRecord*> freeRuleScanRecords;
  /// The maximum number of free-list items to keep.
  const size_t maximumFreeRuleScanRecords = 8096;
  /// The list of blocks (of size \see NumScanRecordsPerBlock) we have
  /// allocated.
  std::vector<RuleScanRecord*> ruleScanRecordBlocks;
  /// The number of records to allocate per block.
  const size_t numScanRecordsPerBlock = 4096;
  /// The buffer positions of the current block.
  RuleScanRecord* currentBlockPos = nullptr, * currentBlockEnd = nullptr;

  RuleScanRecord* newRuleScanRecord() {
    // If we have an item on the free list, return it.
    if (!freeRuleScanRecords.empty()) {
      auto result = freeRuleScanRecords.back();
      freeRuleScanRecords.pop_back();
      return result;
    }

    // If we are at the end of a block, allocate a new one.
    if (currentBlockPos == currentBlockEnd) {
      currentBlockPos = new RuleScanRecord[numScanRecordsPerBlock];
      ruleScanRecordBlocks.push_back(currentBlockPos);
      currentBlockEnd = currentBlockPos + numScanRecordsPerBlock;
    }
    return currentBlockPos++;
  }

  void freeRuleScanRecord(RuleScanRecord* scanRecord) {
    if (freeRuleScanRecords.size() < maximumFreeRuleScanRecords) {
      scanRecord->pausedInputRequests.clear();
      scanRecord->deferredScanRequests.clear();
      freeRuleScanRecords.push_back(scanRecord);
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
  bool scanRule(RuleInfo& ruleInfo) {
    // If the rule is already scanned, we are done.
    if (ruleInfo.isScanned(this))
      return true;

    // If the rule is being scanned, we don't need to do anything.
    if (ruleInfo.isScanning())
      return false;

    // Otherwise, start scanning the rule.
    if (trace)
      trace->checkingRuleNeedsToRun(&ruleInfo.rule);

    // Report the status change.
    if (ruleInfo.rule.updateStatus)
      ruleInfo.rule.updateStatus(Rule::StatusKind::IsScanning);

    // If the rule has never been run, it needs to run.
    if (ruleInfo.result.builtAt == 0) {
      if (trace)
        trace->ruleNeedsToRunBecauseNeverBuilt(&ruleInfo.rule);
      ruleInfo.state = RuleInfo::StateKind::NeedsToRun;
      return true;
    }

    // If the rule indicates its computed value is out of date, it needs to run.
    if (ruleInfo.rule.isResultValid &&
        !ruleInfo.rule.isResultValid(ruleInfo.rule, ruleInfo.result.value)) {
      if (trace)
        trace->ruleNeedsToRunBecauseInvalidValue(&ruleInfo.rule);
      ruleInfo.state = RuleInfo::StateKind::NeedsToRun;
      return true;
    }

    // If the rule has no dependencies, then it is ready to run.
    if (ruleInfo.result.dependencies.empty()) {
      if (trace)
        trace->ruleDoesNotNeedToRun(&ruleInfo.rule);
      ruleInfo.state = RuleInfo::StateKind::DoesNotNeedToRun;
      return true;
    }

    // Otherwise, we need to do a recursive scan of the inputs so enqueue this
    // rule for scanning.
    //
    // We could also take an approach where we enqueue each of the individual
    // inputs, but in my experiments with that approach it has always performed
    // significantly worse.
    if (trace)
      trace->ruleScheduledForScanning(&ruleInfo.rule);
    ruleInfo.state = RuleInfo::StateKind::IsScanning;
    ruleInfo.setPendingScanRecord(newRuleScanRecord());
    ruleInfosToScan.push_back({ &ruleInfo, /*InputIndex=*/0, nullptr });

    return false;
  }

  /// Request the construction of the key specified by the given rule.
  ///
  /// \returns True if the rule is already available, otherwise the rule will be
  /// enqueued for processing.
  bool demandRule(RuleInfo& ruleInfo) {
    // The rule must have already been scanned.
    assert(ruleInfo.isScanned(this));

    // If the rule is complete, we are done.
    if (ruleInfo.isComplete(this))
      return true;

    // If the rule is in progress, we don't need to do anything.
    if (ruleInfo.isInProgress())
      return false;

    // If the rule isn't marked complete, but doesn't need to actually run, then
    // just update it.
    if (ruleInfo.state == RuleInfo::StateKind::DoesNotNeedToRun) {
      ruleInfo.setComplete(this);

      // Report the status change.
      if (ruleInfo.rule.updateStatus)
        ruleInfo.rule.updateStatus(Rule::StatusKind::IsUpToDate);

      return true;
    }

    // Otherwise, we actually need to initiate the processing of this rule.
    assert(ruleInfo.state == RuleInfo::StateKind::NeedsToRun);

    // Create the task for this rule.
    Task* task = ruleInfo.rule.action(buildEngine);

    // Find the task info for this task.
    auto it = taskInfos.find(task);
    assert(it != taskInfos.end() &&
           "rule action returned an unregistered task");
    TaskInfo* taskInfo = &it->second;
    taskInfo->forRuleInfo = &ruleInfo;

    if (trace)
      trace->createdTaskForRule(taskInfo->task.get(), &ruleInfo.rule);

    // Transition the rule state.
    ruleInfo.state = RuleInfo::StateKind::InProgressWaiting;
    ruleInfo.setPendingTaskInfo(taskInfo);

    // Reset the Rule Dependencies, which we just append to during processing,
    // but we reset the others to ensure no one ever inadvertently uses them
    // during an invalid state.
    ruleInfo.result.dependencies.clear();

    // Inform the task it should start.
    task->start(buildEngine);

    // Provide the task the prior result, if present.
    //
    // FIXME: This should perhaps just be lumped in with the start call? Or
    // alternately, maybe there should just be an API call to fetch this, and
    // the clients that want it can ask? It's cheap to provide here, so
    // ultimately this is mostly a matter of cleanliness.
    if (ruleInfo.result.builtAt != 0) {
      task->providePriorValue(buildEngine, ruleInfo.result.value);
    }

    // If this task has no waiters, schedule it immediately for finalization.
    if (!taskInfo->waitCount) {
      readyTaskInfos.push_back(taskInfo);
    }

    return false;
  }

  /// Process an individual scan request.
  ///
  /// This will process all of the inputs required by the requesting rule, in
  /// order, unless the scan needs to be deferred waiting for an input.
  void processRuleScanRequest(RuleScanRequest request) {
    auto& ruleInfo = *request.ruleInfo;

    // If the rule has been completed after this scan request was enqueued,
    // ignore the request.
    if (!ruleInfo.isScanning())
        return;

    // Process each of the remaining inputs.
    do {
      // Look up the input rule info, if not yet cached.
      if (!request.inputRuleInfo) {
        const auto& inputKey = ruleInfo.result.dependencies[request.inputIndex];
        request.inputRuleInfo = &getRuleInfoForKey(inputKey);
      }
      auto& inputRuleInfo = *request.inputRuleInfo;

      // Scan the input.
      bool isScanned = scanRule(inputRuleInfo);

      // If the input isn't scanned yet, enqueue this input scan request.
      if (!isScanned) {
        assert(inputRuleInfo.isScanning());
        if (trace)
          trace->ruleScanningDeferredOnInput(&ruleInfo.rule,
                                             &inputRuleInfo.rule);
        inputRuleInfo.getPendingScanRecord()
          ->deferredScanRequests.push_back(request);

        // We will continue scanning the remainder of the inputs in this pass,
        // so we modify the duplicated scan request so that when it will be
        // individually processed when it is resumed. Note that this relies on
        // the cached input rule info lookup above.
        //
        // FIXME: This is a hack, maybe we should have a separate return queue
        // for these.
        inputRuleInfo.getPendingScanRecord()
          ->deferredScanRequests.back().inputIndex =
            ruleInfo.result.dependencies.size() - 1;
        ruleInfo.getPendingScanRecord()->scanWaitCount++;

        // Continue scanning the remainder of the inputs.
        ++request.inputIndex;
        request.inputRuleInfo = nullptr;
        continue;
      }

      if (trace)
        trace->ruleScanningNextInput(&ruleInfo.rule, &inputRuleInfo.rule);

      // Demand the input.
      bool isAvailable = demandRule(inputRuleInfo);

      // If the input isn't already available, enqueue this scan request on the
      // input.
      if (!isAvailable) {
        if (trace)
          trace->ruleScanningDeferredOnTask(
            &ruleInfo.rule, inputRuleInfo.getPendingTaskInfo()->task.get());
        assert(inputRuleInfo.isInProgress());
        inputRuleInfo.getPendingTaskInfo()->
            deferredScanRequests.push_back(request);
        return;
      }

      // If the input has been computed since the last time this rule was
      // built, it needs to run.
      if (ruleInfo.result.builtAt < inputRuleInfo.result.computedAt) {
        if (trace)
          trace->ruleNeedsToRunBecauseInputRebuilt(
            &ruleInfo.rule, &inputRuleInfo.rule);
        finishScanRequest(ruleInfo, RuleInfo::StateKind::NeedsToRun);
        return;
      }

      // Otherwise, increment the scan index.
      ++request.inputIndex;
      request.inputRuleInfo = nullptr;
    } while (request.inputIndex != ruleInfo.result.dependencies.size());

    // If we have deferred scan requests, wait to be reprocessed.
    if (ruleInfo.getPendingScanRecord()->scanWaitCount != 0)
      return;

    // If we reached the end of the inputs, the rule does not need to run.
    if (trace)
      trace->ruleDoesNotNeedToRun(&ruleInfo.rule);
    finishScanRequest(ruleInfo, RuleInfo::StateKind::DoesNotNeedToRun);
  }

  void finishScanRequest(RuleInfo& inputRuleInfo,
                         RuleInfo::StateKind newState) {
    assert(inputRuleInfo.isScanning());
    auto scanRecord = inputRuleInfo.getPendingScanRecord();

    // Wake up all of the pending scan requests.
    for (const auto& request: scanRecord->deferredScanRequests) {
      ruleInfosToScan.push_back(request);
      request.ruleInfo->getPendingScanRecord()->scanWaitCount--;
    }

    // Wake up all of the input requests on this rule.
    for (const auto& request: scanRecord->pausedInputRequests) {
      inputRequests.push_back(request);
    }

    // Update the rule state.
    freeRuleScanRecord(scanRecord);
    inputRuleInfo.setPendingScanRecord(nullptr);
    inputRuleInfo.state = newState;
  }

  /// Decrement the task's wait count, and move it to the ready queue if
  /// necessary.
  void decrementTaskWaitCount(TaskInfo* taskInfo) {
    --taskInfo->waitCount;
    if (trace)
      trace->updatedTaskWaitCount(taskInfo->task.get(), taskInfo->waitCount);
    if (taskInfo->waitCount == 0) {
      if (trace)
        trace->unblockedTask(taskInfo->task.get());
      readyTaskInfos.push_back(taskInfo);
    }
  }

  /// Execute all of the work pending in the engine queues until they are empty.
  void executeTasks() {
    std::vector<TaskInputRequest> finishedInputRequests;

    // Process requests as long as we have work to do.
    while (true) {
      bool didWork = false;

      // Process all of the pending rule scan requests.
      //
      // FIXME: We don't want to process all of these requests, this amounts to
      // doing all of the dependency scanning up-front.
      while (!ruleInfosToScan.empty()) {
        didWork = true;

        auto request = ruleInfosToScan.back();
        ruleInfosToScan.pop_back();

        processRuleScanRequest(request);
      }

      // Process all of the pending input requests.
      while (!inputRequests.empty()) {
        didWork = true;

        auto request = inputRequests.back();
        inputRequests.pop_back();

        if (trace) {
          if (request.taskInfo) {
            trace->handlingTaskInputRequest(request.taskInfo->task.get(),
                                            &request.inputRuleInfo->rule);
          } else {
            trace->handlingBuildInputRequest(&request.inputRuleInfo->rule);
          }
        }

        // Request the input rule be scanned.
        bool isScanned = scanRule(*request.inputRuleInfo);

        // If the rule is not yet scanned, suspend this input request.
        if (!isScanned) {
          assert(request.inputRuleInfo->isScanning());
          if (trace)
            trace->pausedInputRequestForRuleScan(
              &request.inputRuleInfo->rule);
          request.inputRuleInfo->getPendingScanRecord()
            ->pausedInputRequests.push_back(request);
          continue;
        }

        // Request the input rule be computed.
        bool isAvailable = demandRule(*request.inputRuleInfo);

        // If this is a dummy input request, we are done.
        if (!request.taskInfo)
          continue;

        // If the rule is already available, enqueue the finalize request.
        if (isAvailable) {
          if (trace)
            trace->readyingTaskInputRequest(request.taskInfo->task.get(),
                                            &request.inputRuleInfo->rule);
          finishedInputRequests.push_back(request);
        } else {
          // Otherwise, record the pending input request.
          assert(request.inputRuleInfo->getPendingTaskInfo());
          if (trace)
            trace->addedRulePendingTask(&request.inputRuleInfo->rule,
                                        request.taskInfo->task.get());
          request.inputRuleInfo->getPendingTaskInfo()->requestedBy.push_back(
            request);
        }
      }

      // Process all of the finished inputs.
      while (!finishedInputRequests.empty()) {
        didWork = true;

        auto request = finishedInputRequests.back();
        finishedInputRequests.pop_back();

        if (trace)
          trace->completedTaskInputRequest(request.taskInfo->task.get(),
                                           &request.inputRuleInfo->rule);

        // If this is a must follow input, we simply decrement the task wait
        // count.
        //
        // This works because must follow inputs do not need to be recorded or
        // scanned -- they are only relevant if the task is executing, in which
        // case it is responsible for having supplied the request.
        if (request.inputID == kMustFollowInputID) {
          decrementTaskWaitCount(request.taskInfo);
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
        request.taskInfo->forRuleInfo->result.dependencies.push_back(
          request.inputRuleInfo->rule.key);

        // Provide the requesting task with the input.
        //
        // FIXME: Should we provide the input key here? We have it available
        // cheaply.
        assert(request.inputRuleInfo->isComplete(this));
        request.taskInfo->task->provideValue(
          buildEngine, request.inputID, request.inputRuleInfo->result.value);

        // Decrement the wait count, and move to finish queue if necessary.
        decrementTaskWaitCount(request.taskInfo);
      }

      // Process all of the ready to run tasks.
      while (!readyTaskInfos.empty()) {
        didWork = true;

        TaskInfo* taskInfo = readyTaskInfos.back();
        readyTaskInfos.pop_back();

        RuleInfo* ruleInfo = taskInfo->forRuleInfo;
        assert(taskInfo == ruleInfo->getPendingTaskInfo());

        if (trace)
            trace->readiedTask(taskInfo->task.get(), &ruleInfo->rule);

        // Transition the rule state.
        assert(ruleInfo->isInProgressWaiting());
        ruleInfo->state = RuleInfo::StateKind::InProgressComputing;

        // Inform the task its inputs are ready and it should finish.
        //
        // FIXME: We need to track this state, and generate an error if this
        // task ever requests additional inputs.
        taskInfo->task->inputsAvailable(buildEngine);

        // Increment our count of outstanding tasks.
        ++numOutstandingUnfinishedTasks;
      }

      // Process all of the finished tasks.
      while (true) {
        // Try to take a task from the finished queue.
        TaskInfo* taskInfo = nullptr;
        {
          std::lock_guard<std::mutex> guard(finishedTaskInfosMutex);
          if (!finishedTaskInfos.empty()) {
            taskInfo = finishedTaskInfos.back();
            finishedTaskInfos.pop_back();
          }
        }
        if (!taskInfo)
          break;

        didWork = true;

        RuleInfo* ruleInfo = taskInfo->forRuleInfo;
        assert(taskInfo == ruleInfo->getPendingTaskInfo());

        // The task was changed if was computed in the current iteration.
        if (trace) {
          bool wasChanged = ruleInfo->result.computedAt == currentTimestamp;
          trace->finishedTask(taskInfo->task.get(), &ruleInfo->rule,
                              wasChanged);
        }

        // Transition the rule state by completing the rule (the value itself is
        // stored in the taskIsFinished call).
        assert(ruleInfo->state == RuleInfo::StateKind::InProgressComputing);
        ruleInfo->setPendingTaskInfo(nullptr);
        ruleInfo->setComplete(this);

        // Report the status change.
        if (ruleInfo->rule.updateStatus)
          ruleInfo->rule.updateStatus(Rule::StatusKind::IsComplete);

        // Add all of the task's discovered dependencies.
        //
        // FIXME: We could audit these dependencies at this point to verify that
        // they are not keys for rules which have not been run, which would
        // indicate an underspecified build (e.g., a generated header).
        ruleInfo->result.dependencies.insert(
          ruleInfo->result.dependencies.begin(),
          taskInfo->discoveredDependencies.begin(),
          taskInfo->discoveredDependencies.end());

        // Push back dummy input requests for any discovered dependencies, which
        // must be at least built in order to be brought up-to-date.
        //
        // FIXME: The need to do this makes it questionable that we use this
        // approach for discovered dependencies instead of just providing
        // support for taskNeedsInput() even after the task has started
        // computing and from parallel contexts.
        for (const auto& inputKey: taskInfo->discoveredDependencies) {
          inputRequests.push_back({ nullptr, &getRuleInfoForKey(inputKey) });
        }

        // Update the database record, if attached.
        if (db)
            db->setRuleResult(ruleInfo->rule, ruleInfo->result);

        // Wake up all of the pending scan requests.
        for (const auto& request: taskInfo->deferredScanRequests) {
          ruleInfosToScan.push_back(request);
        }

        // Push all pending input requests onto the work queue.
        if (trace) {
          for (auto& request: taskInfo->requestedBy) {
            trace->readyingTaskInputRequest(request.taskInfo->task.get(),
                                            &request.inputRuleInfo->rule);
          }
        }
        finishedInputRequests.insert(finishedInputRequests.end(),
                                     taskInfo->requestedBy.begin(),
                                     taskInfo->requestedBy.end());

        // Decrement our count of outstanding tasks.
        --numOutstandingUnfinishedTasks;

        // Delete the pending task.
        auto it = taskInfos.find(taskInfo->task.get());
        assert(it != taskInfos.end());
        taskInfos.erase(it);
      }

      // If we haven't done any other work at this point but we have pending
      // tasks, we need to wait for a task to complete.
      if (!didWork && numOutstandingUnfinishedTasks != 0) {
        // Wait for our condition variable.
        std::unique_lock<std::mutex> lock(finishedTaskInfosMutex);

        // Ensure we still don't have enqueued operations under the protection
        // of the mutex, if one has been added then we may have already missed
        // the condition notification and cannot safely wait.
        if (finishedTaskInfos.empty()) {
            finishedTaskInfosCondition.wait(lock);
        }

        didWork = true;
      }

      // If we didn't do any work, we are done.
      if (!didWork)
        break;
    }

    // If there was no work to do, but we still have running tasks, then
    // we have found a cycle and are deadlocked.
    if (!taskInfos.empty()) {
      reportCycle();
    }
  }

  void reportCycle() {
    // Gather all of the successor relationships.
    std::unordered_map<Rule*, std::vector<Rule*>> graph;
    for (const auto& it: taskInfos) {
      const TaskInfo& taskInfo = it.second;
      assert(taskInfo.forRuleInfo);
      std::vector<Rule*> successors;
      for (const auto& request: taskInfo.requestedBy) {
        assert(request.taskInfo->forRuleInfo);
        successors.push_back(&request.taskInfo->forRuleInfo->rule);
      }
      graph.insert({ &taskInfo.forRuleInfo->rule, successors });
    }

    // Find the cycle, which should be reachable from any remaining node.
    //
    // FIXME: Need a setvector.
    std::vector<Rule*> cycleList;
    std::unordered_set<Rule*> cycleItems;
    std::function<bool(Rule*)> findCycle;
    findCycle = [&](Rule* node) {
      // Push the node on the stack.
      cycleList.push_back(node);
      auto it = cycleItems.insert(node);

      // If the node is already in the stack, we found the cycle.
      if (!it.second)
        return true;

      // Otherwise, iterate over each successor looking for a cycle.
      for (Rule* successor: graph[node]) {
        if (findCycle(successor))
          return true;
      }

      // If we didn't find the cycle, pop this item.
      cycleItems.erase(it.first);
      cycleList.pop_back();
      return false;
    };

    // Iterate through the graph keys in a deterministic order.
    std::vector<Rule*> orderedKeys;
    for (auto& entry: graph)
      orderedKeys.push_back(entry.first);
    std::sort(orderedKeys.begin(), orderedKeys.end(), [] (Rule* a, Rule* b) {
        return a->key < b->key;
      });
    for (const auto& key: orderedKeys) {
      if (findCycle(key))
        break;
    }
    assert(!cycleList.empty());

    // Reverse the cycle list, since it was formed from the successor graph.
    std::reverse(cycleList.begin(), cycleList.end());

    delegate.cycleDetected(cycleList);

    // Complete all of the remaining tasks.
    //
    // FIXME: Should we have a task abort callback?
    for (auto& it: taskInfos) {
      // Complete the task, even though it did not update the value.
      //
      // FIXME: What should we do here with the value?
      TaskInfo* taskInfo = &it.second;
      RuleInfo* ruleInfo = taskInfo->forRuleInfo;
      assert(taskInfo == ruleInfo->getPendingTaskInfo());
      ruleInfo->setPendingTaskInfo(nullptr);
      ruleInfo->setComplete(this);
    }

    // Delete all of the tasks.
    taskInfos.clear();
  }

public:
  BuildEngineImpl(class BuildEngine& buildEngine,
                  BuildEngineDelegate& delegate)
    : buildEngine(buildEngine), delegate(delegate) {}

  ~BuildEngineImpl() {
    // If tracing is enabled, close it.
    if (trace) {
      std::string error;
      trace->close(&error);
    }
  }

  RuleInfo& getRuleInfoForKey(const KeyType& key) {
    // Check if we have already found the rule.
    auto it = ruleInfos.find(key);
    if (it != ruleInfos.end())
      return it->second;

    // Otherwise, request it from the delegate and add it.
    return addRule(delegate.lookupRule(key));
  }

  /// @name Rule Definition
  /// @{

  RuleInfo& addRule(Rule&& rule) {
    auto result = ruleInfos.emplace(rule.key, RuleInfo(std::move(rule)));
    if (!result.second) {
      // FIXME: Error handling.
      std::cerr << "error: attempt to register duplicate rule \""
                << rule.key << "\"\n";
      exit(1);
    }

    // If we have a database attached, retrieve any stored result.
    //
    // FIXME: Investigate retrieving this result lazily. If the DB is
    // particularly efficient, it may be best to retrieve this only when we need
    // it and never duplicate it.
    RuleInfo& ruleInfo = result.first->second;
    if (db) {
      db->lookupRuleResult(ruleInfo.rule, &ruleInfo.result);
    }

    return ruleInfo;
  }

  /// @}

  /// @name Client API
  /// @{

  const ValueType& build(const KeyType& key) {
    if (db)
      db->buildStarted();

    // Increment our running iteration count.
    //
    // At this point, we should conceptually mark each complete rule as
    // incomplete. However, instead of doing all that work immediately, we
    // perform it lazily by reusing the Result::builtAt field for each rule as
    // an additional mark. When a rule is demanded, if its builtAt index isn't
    // up-to-date then we lazily reset it to be Incomplete, \see demandRule()
    // and \see RuleInfo::isComplete().
    ++currentTimestamp;

    // Find the rule.
    auto& ruleInfo = getRuleInfoForKey(key);

    if (trace)
      trace->buildStarted();

    // Push a dummy input request for this rule.
    inputRequests.push_back({ nullptr, &ruleInfo });

    // Run the build engine, to process any necessary tasks.
    executeTasks();

    // Update the build database, if attached.
    //
    // FIXME: Is it correct to do this here, or earlier?
    if (db) {
      db->setCurrentIteration(currentTimestamp);
      db->buildComplete();
    }

    if (trace)
      trace->buildEnded();

    // Clear the rule scan free-lists.
    //
    // FIXME: Introduce a per-build context object to hold this.
    for (auto block: ruleScanRecordBlocks)
      delete[] block;
    currentBlockPos = currentBlockEnd = nullptr;
    freeRuleScanRecords.clear();
    ruleScanRecordBlocks.clear();

    // The task queue should be empty and the rule complete.
    assert(taskInfos.empty() && ruleInfo.isComplete(this));
    return ruleInfo.result.value;
  }

  void attachDB(std::unique_ptr<BuildDB> database) {
    assert(!db && "invalid attachDB() call");
    assert(currentTimestamp == 0 && "invalid attachDB() call");
    assert(ruleInfos.empty() && "invalid attachDB() call");
    db = std::move(database);

    // Load our initial state from the database.
    currentTimestamp = db->getCurrentIteration();
  }

  bool enableTracing(const std::string& filename, std::string* error_out) {
    std::unique_ptr<BuildEngineTrace> trace(new BuildEngineTrace());

    if (!trace->open(filename, error_out))
      return false;

    this->trace = std::move(trace);
    return true;
  }

  /// Dump the build state to a file in Graphviz DOT format.
  void dumpGraphToFile(const std::string& path) {
    FILE* fp = ::fopen(path.c_str(), "w");
    if (!fp) {
      // FIXME: Error handling.
      std::cerr << "error: unable to open graph output path \""
                << path << "\"\n";
      exit(1);
    }

    // Write the graph header.
    fprintf(fp, "digraph llbuild {\n");
    fprintf(fp, "rankdir=\"LR\"\n");
    fprintf(fp, "node [fontsize=10, shape=box, height=0.25]\n");
    fprintf(fp, "edge [fontsize=10]\n");
    fprintf(fp, "\n");

    // Create a canonical node ordering.
    std::vector<const RuleInfo*> orderedRuleInfos;
    for (const auto& entry: ruleInfos)
      orderedRuleInfos.push_back(&entry.second);
    std::sort(orderedRuleInfos.begin(), orderedRuleInfos.end(),
              [] (const RuleInfo* a, const RuleInfo* b) {
        return a->rule.key < b->rule.key;
      });

    // Write out all of the rules.
    for (const auto& ruleInfo: orderedRuleInfos) {
      fprintf(fp, "\"%s\"\n", ruleInfo->rule.key.c_str());
      for (auto& input: ruleInfo->result.dependencies) {
        fprintf(fp, "\"%s\" -> \"%s\"\n", ruleInfo->rule.key.c_str(),
                input.c_str());
      }
      fprintf(fp, "\n");
    }

    // Write the footer and close.
    fprintf(fp, "}\n");
    fclose(fp);
  }

  void addTaskInputRequest(Task* task, const KeyType& key, uintptr_t inputID) {
    auto taskinfo_it = taskInfos.find(task);
    assert(taskinfo_it != taskInfos.end() &&
           "cannot request inputs for an unknown task");
    TaskInfo* taskInfo = &taskinfo_it->second;

    // Validate that the task is in a valid state to request inputs.
    if (!taskInfo->forRuleInfo->isInProgressWaiting()) {
      // FIXME: Error handling.
      abort();
    }

    // Lookup the rule for this task.
    RuleInfo* ruleInfo = &getRuleInfoForKey(key);
    
    inputRequests.push_back({ taskInfo, ruleInfo, inputID });
    taskInfo->waitCount++;
  }

  /// @}

  /// @name Task Management Client APIs
  /// @{

  Task* registerTask(Task* task) {
    auto result = taskInfos.emplace(task, TaskInfo(task));
    assert(result.second && "task already registered");
    (void)result;
    return task;
  }

  void taskNeedsInput(Task* task, const KeyType& key, uintptr_t inputID) {
    // Validate the InputID.
    if (inputID > BuildEngine::kMaximumInputID) {
      // FIXME: Error handling.
      std::cerr << "error: attempt to use reserved input ID\n";
      exit(1);
    }

    addTaskInputRequest(task, key, inputID);
  }

  void taskMustFollow(Task* task, const KeyType& key) {
    addTaskInputRequest(task, key, kMustFollowInputID);
  }

  void taskDiscoveredDependency(Task* task, const KeyType& key) {
    // Find the task info.
    //
    // FIXME: This is not safe.
    auto taskinfo_it = taskInfos.find(task);
    assert(taskinfo_it != taskInfos.end() &&
           "cannot request inputs for an unknown task");
    TaskInfo* taskInfo = &taskinfo_it->second;

    if (!taskInfo->forRuleInfo->isInProgressComputing()) {
      // FIXME: Error handling.
      std::cerr << "error: invalid state for adding discovered dependency\n";
      exit(1);
    }

    taskInfo->discoveredDependencies.push_back(key);
  }

  void taskIsComplete(Task* task, ValueType&& value, bool forceChange) {
    // FIXME: We should flag the task to ensure this is only called once, and
    // that no other API calls are made once complete.

    // FIXME: This is not safe.
    auto taskinfo_it = taskInfos.find(task);
    assert(taskinfo_it != taskInfos.end() &&
           "cannot request inputs for an unknown task");
    TaskInfo* taskInfo = &taskinfo_it->second;

    if (!taskInfo->forRuleInfo->isInProgressComputing()) {
      // FIXME: Error handling.
      std::cerr << "error: invalid state for marking task complete\n";
      exit(1);
    }

    RuleInfo* ruleInfo = taskInfo->forRuleInfo;
    assert(taskInfo == ruleInfo->getPendingTaskInfo());

    // Process the provided result.
    if (!forceChange && value == ruleInfo->result.value) {
        // If the value is unchanged, do nothing.
    } else {
        // Otherwise, updated the result and the computed at time.
        ruleInfo->result.value = std::move(value);
        ruleInfo->result.computedAt = currentTimestamp;
    }

    // Enqueue the finished task.
    {
      std::lock_guard<std::mutex> guard(finishedTaskInfosMutex);
      finishedTaskInfos.push_back(taskInfo);
    }

    // Notify the engine to wake up, if necessary.
    finishedTaskInfosCondition.notify_one();
  }

  /// @}

  /// @name Internal APIs
  /// @{

  uint64_t getCurrentTimestamp() const { return currentTimestamp; }

  /// @}
};

}

#pragma mark - BuildEngine

BuildEngine::BuildEngine(BuildEngineDelegate& delegate)
  : impl(new BuildEngineImpl(*this, delegate)) 
{
}

BuildEngine::~BuildEngine() {
  delete static_cast<BuildEngineImpl*>(impl);
}

void BuildEngine::addRule(Rule&& rule) {
  static_cast<BuildEngineImpl*>(impl)->addRule(std::move(rule));
}

const ValueType& BuildEngine::build(const KeyType& key) {
  return static_cast<BuildEngineImpl*>(impl)->build(key);
}

void BuildEngine::dumpGraphToFile(const std::string& path) {
  static_cast<BuildEngineImpl*>(impl)->dumpGraphToFile(path);
}

void BuildEngine::attachDB(std::unique_ptr<BuildDB> database) {
  static_cast<BuildEngineImpl*>(impl)->attachDB(std::move(database));
}

bool BuildEngine::enableTracing(const std::string& path,
                                std::string* error_out) {
  return static_cast<BuildEngineImpl*>(impl)->enableTracing(path, error_out);
}

Task* BuildEngine::registerTask(Task* task) {
  return static_cast<BuildEngineImpl*>(impl)->registerTask(task);
}

void BuildEngine::taskNeedsInput(Task* task, const KeyType& key,
                                 uintptr_t inputID) {
  static_cast<BuildEngineImpl*>(impl)->taskNeedsInput(task, key, inputID);
}

void BuildEngine::taskDiscoveredDependency(Task* task, const KeyType& key) {
  static_cast<BuildEngineImpl*>(impl)->taskDiscoveredDependency(task, key);
}

void BuildEngine::taskMustFollow(Task* task, const KeyType& key) {
  static_cast<BuildEngineImpl*>(impl)->taskMustFollow(task, key);
}

void BuildEngine::taskIsComplete(Task* task, ValueType&& value,
                                 bool forceChange) {
  static_cast<BuildEngineImpl*>(impl)->taskIsComplete(task, std::move(value),
                                                      forceChange);
}
