//===-- BuildEngine.cpp ---------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Core/BuildEngine.h"

#include "llbuild/Basic/Tracing.h"
#include "llbuild/Core/BuildDB.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"

#include "BuildEngineTrace.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
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

  /// The key table, used when there is no database.
  llvm::StringMap<bool> keyTable;

  /// The mutex that protects the key table.
  std::mutex keyTableMutex;
  
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

    RuleInfo(KeyID keyID, Rule&& rule) : keyID(keyID), rule(rule) {}

    /// The ID for the rule key.
    KeyID keyID;

    /// The rule this information describes.
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

    void setCancelled() {
      // If we have to cancel a task, it becomes incomplete. We do not need to
      // sync this to the database, the database won't see an updated record and
      // can continue to maintain the previous view of state -- however, we must
      // mark the internal representation as incomplete because the result is no
      // longer valid.
      state = StateKind::Incomplete;
    }
    
    RuleScanRecord* getPendingScanRecord() {
      assert(isScanning());
      return inProgressInfo.pendingScanRecord;
    }
    const RuleScanRecord* getPendingScanRecord() const {
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
    const TaskInfo* getPendingTaskInfo() const {
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
  // table. This is probably a good idea in any case, because we would benefit
  // from pool allocating RuleInfo instances.
  std::unordered_map<KeyID, RuleInfo> ruleInfos;

  /// Information tracked for executing tasks.
  //
  // FIXME: Keeping this in a side table is very inefficient, we always have to
  // look it up. It might make much more sense to require the Task to have a
  // private field available for our use to store this.
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
    std::vector<KeyID> discoveredDependencies;

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

  /// The tracked information for executing tasks.
  ///
  /// Access to this must be protected via \see taskInfosMutex.
  std::unordered_map<Task*, TaskInfo> taskInfos;

  /// The mutex that protects the task info map.
  std::mutex taskInfosMutex;

  /// The queue of tasks ready to be finalized.
  std::vector<TaskInfo*> readyTaskInfos;

  /// The number of tasks which have been readied but not yet finished.
  unsigned numOutstandingUnfinishedTasks = 0;

  /// The queue of tasks which are complete, accesses to this member variable
  /// must be protected via \see finishedTaskInfosMutex.
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
  /// every \see RuleInfo, that adds up to a substantial chunk of memory which
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
      ruleInfo.rule.updateStatus(buildEngine, Rule::StatusKind::IsScanning);

    // If the rule has never been run, it needs to run.
    if (ruleInfo.result.builtAt == 0) {
      if (trace)
        trace->ruleNeedsToRunBecauseNeverBuilt(&ruleInfo.rule);
      ruleInfo.state = RuleInfo::StateKind::NeedsToRun;
      return true;
    }

    // If the rule indicates its computed value is out of date, it needs to run.
    //
    // FIXME: We should probably try and move this so that it can be done by
    // clients in the background, either by us backgrounding it, or by using a
    // completion model as we do for inputs.
    if (ruleInfo.rule.isResultValid &&
        !ruleInfo.rule.isResultValid(buildEngine, ruleInfo.rule,
                                     ruleInfo.result.value)) {
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
        ruleInfo.rule.updateStatus(buildEngine, Rule::StatusKind::IsUpToDate);

      return true;
    }

    // Otherwise, we actually need to initiate the processing of this rule.
    assert(ruleInfo.state == RuleInfo::StateKind::NeedsToRun);

    // Create the task for this rule.
    Task* task = ruleInfo.rule.action(buildEngine);

    // Find the task info for this task.
    auto taskInfo = getTaskInfo(task);
    assert(taskInfo && "rule action returned an unregistered task");
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
    {
      TracingInterval i(EngineTaskCallbackKind::Start);
      task->start(buildEngine);
    }

    // Provide the task the prior result, if present.
    //
    // FIXME: This should perhaps just be lumped in with the start call? Or
    // alternately, maybe there should just be an API call to fetch this, and
    // the clients that want it can ask? It's cheap to provide here, so
    // ultimately this is mostly a matter of cleanliness.
    if (ruleInfo.result.builtAt != 0) {
      TracingInterval i(EngineTaskCallbackKind::ProvidePriorValue);
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

    assert(ruleInfo.isScanning());

    // Process each of the remaining inputs.
    do {
      // Look up the input rule info, if not yet cached.
      if (!request.inputRuleInfo) {
        const auto& inputID = ruleInfo.result.dependencies[request.inputIndex];
        request.inputRuleInfo = &getRuleInfoForKey(inputID);
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
        return;
      }

      if (trace)
        trace->ruleScanningNextInput(&ruleInfo.rule, &inputRuleInfo.rule);

      // Demand the input.
      bool isAvailable = demandRule(inputRuleInfo);

      // If the input isn't already available, enqueue this scan request on the
      // input.
      //
      // FIXME: We need to continue scanning the rest of the inputs to ensure we
      // are not delaying necessary work. See <rdar://problem/20248283>.
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
  ///
  /// \param buildKey The key to build.
  /// \returns True on success, false if the build could not be completed; the
  /// latter only occurs when the build contains a cycle currently.
  bool executeTasks(const KeyType& buildKey) {
    std::vector<TaskInputRequest> finishedInputRequests;

    // Push a dummy input request for the rule to build.
    inputRequests.push_back({ nullptr, &getRuleInfoForKey(buildKey) });

    // Process requests as long as we have work to do.
    while (true) {
      bool didWork = false;

      // Process all of the pending rule scan requests.
      //
      // FIXME: We don't want to process all of these requests, this amounts to
      // doing all of the dependency scanning up-front.
      while (!ruleInfosToScan.empty()) {
        TracingInterval i(EngineQueueItemKind::RuleToScan);
        
        didWork = true;

        auto request = ruleInfosToScan.back();
        ruleInfosToScan.pop_back();

        processRuleScanRequest(request);
      }

      // Process all of the pending input requests.
      while (!inputRequests.empty()) {
        TracingInterval i(EngineQueueItemKind::InputRequest);
        
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
        TracingInterval i(EngineQueueItemKind::FinishedInputRequest);
        
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
            request.inputRuleInfo->keyID);

        // Provide the requesting task with the input.
        //
        // FIXME: Should we provide the input key here? We have it available
        // cheaply.
        assert(request.inputRuleInfo->isComplete(this));
        {
          TracingInterval i(EngineTaskCallbackKind::ProvideValue);
          request.taskInfo->task->provideValue(
              buildEngine, request.inputID, request.inputRuleInfo->result.value);
        }

        // Decrement the wait count, and move to finish queue if necessary.
        decrementTaskWaitCount(request.taskInfo);
      }

      // Process all of the ready to run tasks.
      while (!readyTaskInfos.empty()) {
        TracingInterval i(EngineQueueItemKind::ReadyTask);
        
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
        {
          TracingInterval i(EngineTaskCallbackKind::InputsAvailable);
          taskInfo->task->inputsAvailable(buildEngine);
        }

        // Increment our count of outstanding tasks.
        ++numOutstandingUnfinishedTasks;
      }

      // Process all of the finished tasks.
      while (true) {
        TracingInterval i(EngineQueueItemKind::FinishedTask);
        
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
          ruleInfo->rule.updateStatus(buildEngine,
                                      Rule::StatusKind::IsComplete);

        // Add all of the task's discovered dependencies.
        //
        // FIXME: We could audit these dependencies at this point to verify that
        // they are not keys for rules which have not been run, which would
        // indicate an underspecified build (e.g., a generated header).
        ruleInfo->result.dependencies.insert(
          ruleInfo->result.dependencies.end(),
          taskInfo->discoveredDependencies.begin(),
          taskInfo->discoveredDependencies.end());

        // Push back dummy input requests for any discovered dependencies, which
        // must be at least built in order to be brought up-to-date.
        //
        // FIXME: The need to do this makes it questionable that we use this
        // approach for discovered dependencies instead of just providing
        // support for taskNeedsInput() even after the task has started
        // computing and from parallel contexts.
        for (KeyID inputID: taskInfo->discoveredDependencies) {
          inputRequests.push_back({ nullptr, &getRuleInfoForKey(inputID) });
        }

        // Update the database record, if attached.
        if (db) {
          std::string error;
          bool result = db->setRuleResult(
              ruleInfo->keyID, ruleInfo->rule, ruleInfo->result, &error);
          if (!result) {
            delegate.error(error);
            cancelRemainingTasks();
            return false;
          }
        }

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
        {
          std::lock_guard<std::mutex> guard(taskInfosMutex);
          auto it = taskInfos.find(taskInfo->task.get());
          assert(it != taskInfos.end());
          taskInfos.erase(it);
        }
      }

      // If we haven't done any other work at this point but we have pending
      // tasks, we need to wait for a task to complete.
      //
      // NOTE: Cancellation also implements this process, if you modify this
      // code please also validate that \see cancelRemainingTasks() is still
      // correct.
      if (!didWork && numOutstandingUnfinishedTasks != 0) {
        TracingInterval i(EngineQueueItemKind::Waiting);
        
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
      reportCycle(buildKey);
      return false;
    }

    return true;
  }

  /// Report the cycle which has called the engine to be unable to make forward
  /// progress.
  ///
  /// \param buildKey The key which was requested to build (the reported cycle
  /// with start with this node).
  void reportCycle(const KeyType& buildKey) {
    // Take all available locks, to ensure we dump a consistent state.
    std::lock_guard<std::mutex> guard1(taskInfosMutex);
    std::lock_guard<std::mutex> guard2(finishedTaskInfosMutex);

    // Gather all of the successor relationships.
    std::unordered_map<Rule*, std::vector<Rule*>> successorGraph;
    std::vector<const RuleScanRecord *> activeRuleScanRecords;
    for (const auto& it: taskInfos) {
      const TaskInfo& taskInfo = it.second;
      assert(taskInfo.forRuleInfo);
      std::vector<Rule*> successors;
      for (const auto& request: taskInfo.requestedBy) {
        assert(request.taskInfo->forRuleInfo);
        successors.push_back(&request.taskInfo->forRuleInfo->rule);
      }
      for (const auto& request: taskInfo.deferredScanRequests) {
        // Add the sucessor for the deferred relationship itself.
        successors.push_back(&request.ruleInfo->rule);
      }
      successorGraph.insert({ &taskInfo.forRuleInfo->rule, successors });
    }

    // Add the pending scan records for every rule.
    //
    // NOTE: There is a very subtle condition around this versus adding the ones
    // accessible via the tasks, see https://bugs.swift.org/browse/SR-1948.
    // Unfortunately, we do not have a test case for this!
    for (const auto& it: ruleInfos) {
      const RuleInfo& ruleInfo = it.second;
      if (ruleInfo.isScanning()) {
        const auto* scanRecord = ruleInfo.getPendingScanRecord();
        activeRuleScanRecords.push_back(scanRecord);
      }
    }
      
    // Gather dependencies from all of the active scan records.
    std::unordered_set<const RuleScanRecord*> visitedRuleScanRecords;
    while (!activeRuleScanRecords.empty()) {
      const auto* record = activeRuleScanRecords.back();
      activeRuleScanRecords.pop_back();
          
      // Mark the record and ignore it if not scanned.
      if (!visitedRuleScanRecords.insert(record).second)
        continue;
          
      // For each paused request, add the dependency.
      for (const auto& request: record->pausedInputRequests) {
        if (request.taskInfo) {
          successorGraph[&request.inputRuleInfo->rule].push_back(
              &request.taskInfo->forRuleInfo->rule);
        }
      }
          
      // Process the deferred scan requests.
      for (const auto& request: record->deferredScanRequests) {
        // Add the sucessor for the deferred relationship itself.
        successorGraph[&request.inputRuleInfo->rule].push_back(
            &request.ruleInfo->rule);
              
        // Add the active rule scan record which needs to be traversed.
        assert(request.ruleInfo->isScanning());
        activeRuleScanRecords.push_back(
            request.ruleInfo->getPendingScanRecord());
      }
    }

    // Invert the graph, so we can search from the root.
    std::unordered_map<Rule*, std::vector<Rule*>> predecessorGraph;
    for (auto& entry: successorGraph) {
      Rule* node = entry.first;
      for (auto& succ: entry.second) {
        predecessorGraph[succ].push_back(node);
      }
    }

    // Normalize predecessor order, to ensure a deterministic result (at least,
    // if the graph reaches the same cycle).
    for (auto& entry: predecessorGraph) {
      std::sort(entry.second.begin(), entry.second.end(), [](Rule* a, Rule* b) {
          return a->key < b->key;
        });
    }
    
    // Find the cycle by searching from the entry node.
    struct WorkItem {
      WorkItem(Rule * node) { this->node = node; }

      Rule* node;
      unsigned predecessorIndex = 0;
    };
    std::vector<Rule*> cycleList;
    std::unordered_set<Rule*> cycleItems;
    std::vector<WorkItem> stack{
      WorkItem{ &getRuleInfoForKey(buildKey).rule } };
    while (!stack.empty()) {
      // Take the top item.
      auto& entry = stack.back();
      const auto& predecessors = predecessorGraph[entry.node];
      
      // If the index is 0, we just started visiting the node.
      if (entry.predecessorIndex == 0) {
        // Push the node on the stack.
        cycleList.push_back(entry.node);
        auto it = cycleItems.insert(entry.node);

        // If the node is already in the stack, we found a cycle.
        if (!it.second)
          break;
      }

      // Visit the next predecessor, if possible.
      if (entry.predecessorIndex != predecessors.size()) {
        auto* child = predecessors[entry.predecessorIndex];
        entry.predecessorIndex += 1;
        stack.emplace_back(WorkItem{ child });
        continue;
      }

      // Otherwise, we are done visiting this node.
      cycleItems.erase(entry.node);
      cycleList.pop_back();
      stack.pop_back();
    }
    assert(!cycleList.empty());

    delegate.cycleDetected(cycleList);
    cancelRemainingTasks();
  }

  // Cancel all of the remaining tasks.
  void cancelRemainingTasks() {
    // We need to wait for any currently running tasks to be reported as
    // complete. Not doing this would mean we could get asynchronous calls
    // attempting to modify the task state concurrently with the cancellation
    // process, which isn't something we want to need to synchronize on.
    //
    // We don't process the requests at all, we simply drain them. In practice,
    // we expect clients to implement cancellation in conjection with causing
    // long-running tasks to also cancel and fail, so preserving those results
    // is not valuable.
    while (numOutstandingUnfinishedTasks != 0) {
        std::unique_lock<std::mutex> lock(finishedTaskInfosMutex);
        if (finishedTaskInfos.empty()) {
          finishedTaskInfosCondition.wait(lock);
        } else {
          assert(finishedTaskInfos.size() <= numOutstandingUnfinishedTasks);
          numOutstandingUnfinishedTasks -= finishedTaskInfos.size();
          finishedTaskInfos.clear();
        }
    }
    
    for (auto& it: taskInfos) {
      // Cancel the task, marking it incomplete.
      //
      // This will force it to rerun in a later build, but since it was already
      // running in this build that was almost certainly going to be
      // required. Technically, there are rare situations where it wouldn't have
      // to rerun (e.g., if resultIsValid becomes true after being false in this
      // run), and if we were willing to restore the tasks state--either by
      // keeping the old one or by restoring from the database--we could ensure
      // that doesn't happen.
      //
      // NOTE: Actually, we currently don't sync this write to the database, so
      // in some cases we do actually preserve this information (if the client
      // ends up cancelling, then reloading froom the database).
      TaskInfo* taskInfo = &it.second;
      RuleInfo* ruleInfo = taskInfo->forRuleInfo;
      assert(taskInfo == ruleInfo->getPendingTaskInfo());
      ruleInfo->setPendingTaskInfo(nullptr);
      ruleInfo->setCancelled();
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

  BuildEngineDelegate* getDelegate() {
    return &delegate;
  }

  Timestamp getCurrentTimestamp() {
    return currentTimestamp;
  }

  KeyID getKeyID(const KeyType& key) {
    // Delegate if we have a database.
    if (db) {
      std::string error;
      KeyID id = db->getKeyID(key, &error);
      if (!error.empty()) {
        delegate.error(error);
        cancelRemainingTasks();
      }
      return id;
    }

    // Otherwise use our builtin key table.
    {
      std::lock_guard<std::mutex> guard(keyTableMutex);
      auto it = keyTable.insert(std::make_pair(key, false)).first;
      return (KeyID)(uintptr_t)it->getKey().data();
    }
  }

  RuleInfo& getRuleInfoForKey(const KeyType& key) {
    auto keyID = getKeyID(key);
    
    // Check if we have already found the rule.
    auto it = ruleInfos.find(keyID);
    if (it != ruleInfos.end())
      return it->second;

    // Otherwise, request it from the delegate and add it.
    return addRule(keyID, delegate.lookupRule(key));
  }

  RuleInfo& getRuleInfoForKey(KeyID keyID) {
    // Check if we have already found the rule.
    auto it = ruleInfos.find(keyID);
    if (it != ruleInfos.end())
      return it->second;

    // Otherwise, we need to resolve the full key so we can request it from the
    // delegate.
    KeyType key;
    if (db) {
      key = db->getKeyForID(keyID);
    } else {
      // Note that we don't need to lock `keyTable` here because the key entries
      // themselves don't change once created.
      key = llvm::StringMapEntry<bool>::GetStringMapEntryFromKeyData(
          (const char*)(uintptr_t)keyID).getKey();
    }
    return addRule(keyID, delegate.lookupRule(key));
  }

  TaskInfo* getTaskInfo(Task* task) {
    std::lock_guard<std::mutex> guard(taskInfosMutex);
    auto it = taskInfos.find(task);
    return it == taskInfos.end() ? nullptr : &it->second;
  }
  
  /// @name Rule Definition
  /// @{

  RuleInfo& addRule(Rule&& rule) {
    return addRule(getKeyID(rule.key), std::move(rule));
  }
  
  RuleInfo& addRule(KeyID keyID, Rule&& rule) {
    auto result = ruleInfos.emplace(keyID, RuleInfo(keyID, std::move(rule)));
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
      std::string error;
      db->lookupRuleResult(ruleInfo.keyID, ruleInfo.rule, &ruleInfo.result, &error);
      if (!error.empty()) {
        delegate.error(error);
        cancelRemainingTasks();
      }
    }

    return ruleInfo;
  }

  /// @}

  /// @name Client API
  /// @{

  const ValueType& build(const KeyType& key) {
    if (db) {
      std::string error;
      bool result = db->buildStarted(&error);
      if (!result) {
        delegate.error(error);
        static ValueType emptyValue{};
        return emptyValue;
      }
    }

    // Increment our running iteration count.
    //
    // At this point, we should conceptually mark each complete rule as
    // incomplete. However, instead of doing all that work immediately, we
    // perform it lazily by reusing the Result::builtAt field for each rule as
    // an additional mark. When a rule is demanded, if its builtAt index isn't
    // up-to-date then we lazily reset it to be Incomplete, \see demandRule()
    // and \see RuleInfo::isComplete().
    ++currentTimestamp;

    if (trace)
      trace->buildStarted();

    // Run the build engine, to process any necessary tasks.
    bool success = executeTasks(key);
    
    // Update the build database, if attached.
    //
    // FIXME: Is it correct to do this here, or earlier?
    if (db) {
      std::string error;
      bool result = db->setCurrentIteration(currentTimestamp, &error);
      if (!result) {
        delegate.error(error);
        static ValueType emptyValue{};
        return emptyValue;
      }
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

    // If the build failed, return the empty result.
    if (!success) {
      static ValueType emptyValue{};
      return emptyValue;
    }

    // The task queue should be empty and the rule complete.
    auto& ruleInfo = getRuleInfoForKey(key);
    assert(taskInfos.empty() && ruleInfo.isComplete(this));
    return ruleInfo.result.value;
  }

  bool attachDB(std::unique_ptr<BuildDB> database, std::string* error_out) {
    assert(!db && "invalid attachDB() call");
    assert(currentTimestamp == 0 && "invalid attachDB() call");
    assert(ruleInfos.empty() && "invalid attachDB() call");
    db = std::move(database);

    // Load our initial state from the database.
    bool success;
    currentTimestamp = db->getCurrentIteration(&success, error_out);
    return success;
  }

  bool enableTracing(const std::string& filename, std::string* error_out) {
    auto trace = llvm::make_unique<BuildEngineTrace>();

    if (!trace->open(filename, error_out))
      return false;

    this->trace = std::move(trace);
    return true;
  }

  /// Dump the build state to a file in Graphviz DOT format.
  void dumpGraphToFile(const std::string& path) {
    FILE* fp = ::fopen(path.c_str(), "w");
    if (!fp) {
      delegate.error("error: unable to open graph output path \"" + path + "\"");
      return;
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
      for (KeyID inputID: ruleInfo->result.dependencies) {
        const auto& dependency = getRuleInfoForKey(inputID);
        fprintf(fp, "\"%s\" -> \"%s\"\n", ruleInfo->rule.key.c_str(),
                dependency.rule.key.c_str());
      }
      fprintf(fp, "\n");
    }

    // Write the footer and close.
    fprintf(fp, "}\n");
    fclose(fp);
  }

  void addTaskInputRequest(Task* task, const KeyType& key, uintptr_t inputID) {
    auto taskInfo = getTaskInfo(task);

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
    {
      std::lock_guard<std::mutex> guard(taskInfosMutex);
      auto result = taskInfos.emplace(task, TaskInfo(task));
      assert(result.second && "task already registered");
      (void)result;
    }
    return task;
  }

  void taskNeedsInput(Task* task, const KeyType& key, uintptr_t inputID) {
    // Validate the InputID.
    if (inputID > BuildEngine::kMaximumInputID) {
      delegate.error("error: attempt to use reserved input ID");
      cancelRemainingTasks();
      return;
    }

    addTaskInputRequest(task, key, inputID);
  }

  void taskMustFollow(Task* task, const KeyType& key) {
    addTaskInputRequest(task, key, kMustFollowInputID);
  }

  void taskDiscoveredDependency(Task* task, const KeyType& key) {
    // Find the task info.
    auto taskInfo = getTaskInfo(task);
    assert(taskInfo && "cannot request inputs for an unknown task");

    if (!taskInfo->forRuleInfo->isInProgressComputing()) {
      delegate.error("error: invalid state for adding discovered dependency");
      cancelRemainingTasks();
      return;
    }

    auto dependencyID = getKeyID(key);
    taskInfo->discoveredDependencies.push_back(dependencyID);
  }

  void taskIsComplete(Task* task, ValueType&& value, bool forceChange) {
    // FIXME: We should flag the task to ensure this is only called once, and
    // that no other API calls are made once complete.

    auto taskInfo = getTaskInfo(task);
    assert(taskInfo && "cannot request inputs for an unknown task");

    if (!taskInfo->forRuleInfo->isInProgressComputing()) {
      delegate.error("error: invalid state for marking task complete");
      cancelRemainingTasks();
      return;
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

BuildEngineDelegate* BuildEngine::getDelegate() {
  return static_cast<BuildEngineImpl*>(impl)->getDelegate();
}

Timestamp BuildEngine::getCurrentTimestamp() {
  return static_cast<BuildEngineImpl*>(impl)->getCurrentTimestamp();
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

bool BuildEngine::attachDB(std::unique_ptr<BuildDB> database, std::string* error_out) {
  return static_cast<BuildEngineImpl*>(impl)->attachDB(std::move(database), error_out);
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
