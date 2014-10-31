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
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

Task::~Task() {}

#pragma mark - BuildEngine Implementation

namespace {

class BuildEngineImpl {
  struct TaskInfo;

  BuildEngine &BuildEngine;


  /// The build database, if attached.
  std::unique_ptr<BuildDB> DB;

  /// The tracing implementation, if enabled.
  std::unique_ptr<BuildEngineTrace> Trace;

  /// The current build iteration, used to sequentially timestamp build results.
  uint64_t CurrentTimestamp = 0;

  /// The map of rule information.
  struct RuleInfo {
    RuleInfo(Rule &&Rule) : Rule(Rule) {}

    Rule Rule;
    /// The task computing this rule, if in progress.
    TaskInfo* PendingTaskInfo = 0;
    // FIXME: Needs to move to database.
    Result Result = {};

  public:
    bool isComplete(const BuildEngineImpl* Engine) {
      return Result.BuiltAt == Engine->getCurrentTimestamp();
    }
  };
  std::unordered_map<KeyType, RuleInfo> RuleInfos;

  /// The queue of input requests to process.
  struct TaskInputRequest {
    /// The task making the request.
    TaskInfo* TaskInfo;
    /// The rule for the input which was requested.
    RuleInfo* RuleInfo;
    /// The task provided input ID, for its own use in identifying the input.
    uint64_t InputID;
  };
  std::vector<TaskInputRequest> InputRequests;

  /// The set of pending tasks.
  struct TaskInfo {
    TaskInfo(Task* Task) : Task(Task) {}

    std::unique_ptr<Task> Task;
    std::vector<TaskInputRequest> RequestedBy;
    unsigned WaitCount = 0;
    RuleInfo* ForRuleInfo = nullptr;
  };
  std::unordered_map<Task*, TaskInfo> TaskInfos;

  /// The queue of tasks ready to be finalized.
  std::vector<TaskInfo*> ReadyTaskInfos;

private:
  /// @name Build Execution
  /// @{

  /// Check whether the given rule needs to be run in the current environment.
  //
  // FIXME: This function can end doing an unbounded amount of work (scanning
  // the entire dependency graph), which means it will block the execution loop
  // from doing other tasks while it is proceeding. We might want to break it
  // down into small individual blocks of work that we queue and evaluate as
  // part of the normal execution loop.
  bool ruleNeedsToRun(RuleInfo& RuleInfo) {
    if (Trace)
      Trace->checkingRuleNeedsToRun(&RuleInfo.Rule);

    // If the rule has never been run, it needs to run.
    if (RuleInfo.Result.BuiltAt == 0) {
      if (Trace)
        Trace->ruleNeedsToRunBecauseNeverBuilt(&RuleInfo.Rule);
      return true;
    }

    // If the rule indicates it's computed value is out of date, it needs to
    // run.
    if (RuleInfo.Rule.IsResultValid &&
        !RuleInfo.Rule.IsResultValid(RuleInfo.Rule, RuleInfo.Result.Value)) {
      if (Trace)
        Trace->ruleNeedsToRunBecauseInvalidValue(&RuleInfo.Rule);
      return true;
    }

    // Otherwise, if the last time the rule was built is earlier than the time
    // any of its inputs were computed, then it needs to run.
    for (auto& InputKey: RuleInfo.Result.Dependencies) {
      auto it = RuleInfos.find(InputKey);
      if (it == RuleInfos.end()) {
        // FIXME: What do we do here?
        assert(0 && "prior input dependency no longer exists");
        abort();
      }
      auto& InputRuleInfo = it->second;

      // Demand the input.
      //
      // FIXME: Eliminate this unbounded recursion here.
      //
      // FIXME: There is possibility for a cycle here. We need more state bits,
      // I think.
      bool IsAvailable = demandRule(InputRuleInfo);

      // If the input wasn't already available, it needs to run.
      if (!IsAvailable) {
        // FIXME: This is just wrong, just because we haven't run the task yet
        // doesn't necessarily mean that this rule needs to run, if running the
        // task results in an output that hasn't changed (and so ComputedAt
        // isn't updated). This case doesn't come up until we support BuiltAt !=
        // ComputedAt, though.
        if (Trace)
          Trace->ruleNeedsToRunBecauseInputUnavailable(
            &RuleInfo.Rule, &InputRuleInfo.Rule);
        return true;
      }

      // If the input has been computed since the last time this rule was built,
      // it needs to run.
      if (RuleInfo.Result.BuiltAt < InputRuleInfo.Result.ComputedAt) {
        if (Trace)
          Trace->ruleNeedsToRunBecauseInputRebuilt(
            &RuleInfo.Rule, &InputRuleInfo.Rule);
        return true;
      }
    }

    if (Trace)
      Trace->ruleDoesNotNeedToRun(&RuleInfo.Rule);
    return false;
  }

  /// Request the construction of the key specified by the given rule.
  ///
  /// \returns True if the rule is already available, otherwise the rule will be
  /// enqueue for processing.
  bool demandRule(RuleInfo& RuleInfo) {
    // If the rule is complete, we are done.
    if (RuleInfo.isComplete(this))
      return true;

    // If the rule isn't complete, but it already has a pending task, we don't
    // need to do anything.
    if (RuleInfo.PendingTaskInfo)
      return false;

    // If the rule isn't marked complete, but doesn't need to actually run, then
    // just update it.
    if (!ruleNeedsToRun(RuleInfo)) {
      RuleInfo.Result.BuiltAt = CurrentTimestamp;
      assert(RuleInfo.isComplete(this));

      // FIXME: We don't actually tell the DB to update the entry here, because
      // it is essentially just a mark of up-to-dateness, and we will increment
      // it every time. What we should be doing is using a separate flag to
      // track this, instead of abusing BuiltAt for it (see also the FIXME in
      // the declaration of Result::BuiltAt).

      return true;
    }

    // Otherwise, we actually need to initiate the processing of this rule.

    // Create the task for this rule.
    Task* Task = RuleInfo.Rule.Action(BuildEngine);

    // Find the task info for this task.
    auto it = TaskInfos.find(Task);
    assert(it != TaskInfos.end() &&
           "rule action returned an unregistered task");
    TaskInfo* TaskInfo = &it->second;
    RuleInfo.PendingTaskInfo = TaskInfo;
    TaskInfo->ForRuleInfo = &RuleInfo;

    if (Trace)
      Trace->createdTaskForRule(TaskInfo->Task.get(), &RuleInfo.Rule);

    // Reset the Rule result state. The only field we must reset here is the
    // Dependencies, which we just append to during processing, but we reset the
    // others to ensure no one ever inadvertently uses them during an invalid
    // state.
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

  void executeTasks() {
    std::vector<TaskInputRequest> FinishedInputRequests;

    // Process requests as long as we have work to do.
    while (!InputRequests.empty() || !FinishedInputRequests.empty() ||
           !ReadyTaskInfos.empty()) {
      // Process all of the pending input requests.
      while (!InputRequests.empty()) {
        auto Request = InputRequests.back();
        InputRequests.pop_back();

        if (Trace)
          Trace->handlingTaskInputRequest(Request.TaskInfo->Task.get(),
                                          &Request.RuleInfo->Rule);

        // Request the rule be computed.
        bool IsAvailable = demandRule(*Request.RuleInfo);

        // If the rule is already available, enqueue the finalize request.
        if (IsAvailable) {
          if (Trace)
            Trace->readyingTaskInputRequest(Request.TaskInfo->Task.get(),
                                            &Request.RuleInfo->Rule);
          FinishedInputRequests.push_back(Request);
        } else {
          // Otherwise, record the pending input request.
          assert(Request.RuleInfo->PendingTaskInfo != nullptr);
          Request.RuleInfo->PendingTaskInfo->RequestedBy.push_back(Request);
          if (Trace)
            Trace->addedRulePendingTask(&Request.RuleInfo->Rule,
                                        Request.TaskInfo->Task.get());
        }
      }

      // Process all of the finished inputs.
      while (!FinishedInputRequests.empty()) {
        auto Request = FinishedInputRequests.back();
        FinishedInputRequests.pop_back();

        if (Trace)
          Trace->completedTaskInputRequest(Request.TaskInfo->Task.get(),
                                           &Request.RuleInfo->Rule);

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
          Request.RuleInfo->Rule.Key);

        // Provide the requesting task with the input.
        //
        // FIXME: Should we provide the input key here? We have it available
        // cheaply.
        assert(Request.RuleInfo->isComplete(this));
        Request.TaskInfo->Task->provideValue(BuildEngine, Request.InputID,
                                             Request.RuleInfo->Result.Value);

        // Decrement the wait count, and move to finish queue if necessary.
        --Request.TaskInfo->WaitCount;
        if (Trace)
          Trace->updatedTaskWaitCount(Request.TaskInfo->Task.get(),
                                      Request.TaskInfo->WaitCount);
        if (Request.TaskInfo->WaitCount == 0) {
          if (Trace)
            Trace->unblockedTask(Request.TaskInfo->Task.get());
          ReadyTaskInfos.push_back(Request.TaskInfo);
        }
      }

      // Process all of the finished tasks.
      while (!ReadyTaskInfos.empty()) {
        TaskInfo* TaskInfo = ReadyTaskInfos.back();
        ReadyTaskInfos.pop_back();

        RuleInfo* RuleInfo = TaskInfo->ForRuleInfo;
        assert(TaskInfo == RuleInfo->PendingTaskInfo);

        if (Trace)
            Trace->finishedTask(TaskInfo->Task.get(), &RuleInfo->Rule);

        // Inform the task it should finish.
        ValueType Value = TaskInfo->Task->finish();

        // Complete the rule.
        RuleInfo->Result.Value = Value;
        RuleInfo->Result.ComputedAt = CurrentTimestamp;
        RuleInfo->Result.BuiltAt = CurrentTimestamp;
        RuleInfo->PendingTaskInfo = nullptr;

        // Update the database record, if attached.
        if (DB)
            DB->setRuleResult(RuleInfo->Rule, RuleInfo->Result);

        // Push all pending input requests onto the work queue.
        if (Trace) {
          for (auto& Request: TaskInfo->RequestedBy) {
            Trace->readyingTaskInputRequest(Request.TaskInfo->Task.get(),
                                            &Request.RuleInfo->Rule);
          }
        }
        FinishedInputRequests.insert(FinishedInputRequests.end(),
                                     TaskInfo->RequestedBy.begin(),
                                     TaskInfo->RequestedBy.end());

        // Delete the pending task.
        auto it = TaskInfos.find(TaskInfo->Task.get());
        assert(it != TaskInfos.end());
        TaskInfos.erase(it);
      }
    }

    // FIXME: If there was no work to do, but we still have running tasks, then
    // we have found a cycle and are deadlocked.
  }

public:
  BuildEngineImpl(class BuildEngine& BuildEngine) : BuildEngine(BuildEngine) {}

  ~BuildEngineImpl() {
      // If tracing is enabled, close it.
      if (Trace) {
          std::string Error;
          Trace->close(&Error);
      }
  }

  /// @name Rule Definition
  /// @{

  void addRule(Rule &&Rule) {
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
    if (DB) {
        RuleInfo& RuleInfo = Result.first->second;
        DB->lookupRuleResult(RuleInfo.Rule, &RuleInfo.Result);
    }
  }

  /// @}

  /// @name Client API
  /// @{

  ValueType build(KeyType Key) {
    if (DB)
      DB->buildStarted();

    // Increment our running iteration count.
    ++CurrentTimestamp;

    // Find the rule.
    auto it = RuleInfos.find(Key);
    if (it == RuleInfos.end()) {
      // FIXME: Error handling.
      std::cerr << "error: attempt to build unknown rule \"" << Key << "\"\n";
      exit(1);
    }
    auto& RuleInfo = it->second;

    // Demand the result for this rule.
    demandRule(RuleInfo);

    // Run the build engine, to process any necessary tasks.
    executeTasks();

    // Update the build database, if attached.
    //
    // FIXME: Is it correct to do this here, or earlier?
    if (DB) {
      DB->setCurrentIteration(CurrentTimestamp);
      DB->buildComplete();
    }

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

  /// @}

  /// @name Task Management Client APIs
  /// @{

  Task* registerTask(Task* Task) {
    auto Result = TaskInfos.emplace(Task, TaskInfo(Task));
    assert(Result.second && "task already registered");
    (void)Result;
    return Task;
  }

  void taskNeedsInput(Task* Task, KeyType Key, uint32_t InputID) {
    auto taskinfo_it = TaskInfos.find(Task);
    assert(taskinfo_it != TaskInfos.end() &&
           "cannot request inputs for an unknown task");
    TaskInfo* TaskInfo = &taskinfo_it->second;

    // Lookup the rule for this task.
    auto ruleinfo_it = RuleInfos.find(Key);
    if (ruleinfo_it == RuleInfos.end()) {
      // FIXME: Error handling.
      std::cerr << "error: attempt to build unknown rule \"" << Key << "\"\n";
      exit(1);
    }
    RuleInfo* RuleInfo = &ruleinfo_it->second;

    InputRequests.push_back({ TaskInfo, RuleInfo, InputID });
    TaskInfo->WaitCount++;
  }

  /// @}

  /// @name Internal APIs
  /// @{

  uint64_t getCurrentTimestamp() const { return CurrentTimestamp; }

  /// @}
};

}

#pragma mark - BuildEngine

BuildEngine::BuildEngine() : Impl(new BuildEngineImpl(*this)) {
}

BuildEngine::~BuildEngine() {
  delete static_cast<BuildEngineImpl*>(Impl);
}

void BuildEngine::addRule(Rule &&Rule) {
  return static_cast<BuildEngineImpl*>(Impl)->addRule(std::move(Rule));
}

ValueType BuildEngine::build(KeyType Key) {
  return static_cast<BuildEngineImpl*>(Impl)->build(Key);
}

void BuildEngine::attachDB(std::unique_ptr<BuildDB> Database) {
  return static_cast<BuildEngineImpl*>(Impl)->attachDB(std::move(Database));
}

bool BuildEngine::enableTracing(const std::string& Path,
                                std::string* Error_Out) {
  return static_cast<BuildEngineImpl*>(Impl)->enableTracing(Path, Error_Out);
}

Task* BuildEngine::registerTask(Task* Task) {
  return static_cast<BuildEngineImpl*>(Impl)->registerTask(Task);
}

void BuildEngine::taskNeedsInput(Task* Task, KeyType Key, uintptr_t InputID) {
  return static_cast<BuildEngineImpl*>(Impl)->taskNeedsInput(Task, Key,
                                                             InputID);
}

