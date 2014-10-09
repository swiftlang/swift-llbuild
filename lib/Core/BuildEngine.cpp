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

#include <cassert>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace llbuild;
using namespace llbuild::core;

// FIXME: Rip this out or move elsewhere.
#ifdef DEBUG
std::ostream &dbgs() {
  return std::cerr;
}
#else
struct nullstream {
  template<typename T>
  nullstream operator<<(T Input) { return nullstream(); };
};
nullstream dbgs() {
  return nullstream();
}
#endif

Task::~Task() {
  dbgs() << "    build: deleting task (\"" << Name << "\")\n";
}

#pragma mark - BuildEngine Implementation

namespace {

class BuildEngineImpl {
  struct TaskInfo;

  BuildEngine &BuildEngine;

  /// The map of rule information.
  struct RuleInfo {
    RuleInfo(Rule &&Rule) : Rule(Rule) {}

    Rule Rule;
    TaskInfo* PendingTaskInfo = 0;
    ValueType Result = {};
    bool IsComplete = false;
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
    RuleInfo *ForRuleInfo = nullptr;
  };
  std::unordered_map<Task*, TaskInfo> TaskInfos;

  /// The queue of tasks ready to be finalized.
  std::vector<TaskInfo*> ReadyTaskInfos;

private:
  /// @name Build Execution
  /// @{

  void beginRule(RuleInfo& RuleInfo) {
    assert(RuleInfo.PendingTaskInfo == nullptr && "rule already started");

    dbgs() << "    build: creating task for rule \""
           << RuleInfo.Rule.Key << "\"\n";

    // Create the task for this rule.
    Task* Task = RuleInfo.Rule.Action(BuildEngine);

    // Find the task info for this task.
    auto it = TaskInfos.find(Task);
    assert(it != TaskInfos.end() &&
           "rule action returned an unregistered task");
    TaskInfo* TaskInfo = &it->second;
    RuleInfo.PendingTaskInfo = TaskInfo;
    TaskInfo->ForRuleInfo = &RuleInfo;

    dbgs() << "    build: ... created task "
           << TaskInfo->Task.get() << "(\"" << TaskInfo->Task->Name << "\")\n";

    // Inform the task it should start.
    Task->start(BuildEngine);

    // If this task has no waiters, schedule it immediately for finalization.
    if (!TaskInfo->WaitCount) {
      ReadyTaskInfos.push_back(TaskInfo);
    }
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

        dbgs() << "    build: processing pending input request by task "
               << Request.TaskInfo->Task.get() << "(\""
               << Request.TaskInfo->Task->Name
               << "\") for rule \"" << Request.RuleInfo->Rule.Key
               << "\"\n";

        // If the rule is complete, enqueue the finalize request.
        if (Request.RuleInfo->IsComplete) {
          dbgs() << "    build: ... moved it to finished input queue\n";
          FinishedInputRequests.push_back(Request);
          continue;
        }

        // Start the rule, if necessary.
        if (!Request.RuleInfo->PendingTaskInfo) {
          dbgs() << "    build: ... starting its input\n";
          beginRule(*Request.RuleInfo);
        }

        // Record the pending input request.
        assert(Request.RuleInfo->PendingTaskInfo != nullptr);
        Request.RuleInfo->PendingTaskInfo->RequestedBy.push_back(Request);
        dbgs() << "    build: ... added it to the pending queue for "
               << Request.RuleInfo->Rule.Key << "\n";
      }

      // Process all of the finished inputs.
      while (!FinishedInputRequests.empty()) {
        auto Request = FinishedInputRequests.back();
        FinishedInputRequests.pop_back();

        dbgs() << "    build: processing completed input request by task "
               << Request.TaskInfo->Task.get() << "(\""
               << Request.TaskInfo->Task->Name
               << "\") for rule \"" << Request.RuleInfo->Rule.Key
               << "\"\n";

        // Provide the requesting task with the input.
        assert(Request.RuleInfo->IsComplete);
        Request.TaskInfo->Task->provideValue(BuildEngine, Request.InputID,
                                             Request.RuleInfo->Result);

        // Decrement the wait count, and move to finish queue if necessary.
        --Request.TaskInfo->WaitCount;
        dbgs() << "    build: ... this task is now waiting on "
               << Request.TaskInfo->WaitCount
               << " remaining inputs\n";
        if (Request.TaskInfo->WaitCount == 0) {
          dbgs() << "    build: ... unblocked, scheduling finalize for task "
                 << Request.TaskInfo->Task->Name << "\n";
          ReadyTaskInfos.push_back(Request.TaskInfo);
        }
      }

      // Process all of the finished tasks.
      while (!ReadyTaskInfos.empty()) {
        TaskInfo* TaskInfo = ReadyTaskInfos.back();
        ReadyTaskInfos.pop_back();

        // If this is the dummy task, do nothing.
        if (!TaskInfo->ForRuleInfo)
          break;

        RuleInfo* RuleInfo = TaskInfo->ForRuleInfo;
        assert(TaskInfo == RuleInfo->PendingTaskInfo);

        dbgs() << "    build: processing finished task \""
               << TaskInfo->Task->Name
               << "\" computing rule \"" << RuleInfo->Rule.Key << "\"\n";

        // Inform the task it should finish.
        ValueType Result = TaskInfo->Task->finish();

        // Complete the rule.
        RuleInfo->Result = Result;
        RuleInfo->IsComplete = true;
        RuleInfo->PendingTaskInfo = nullptr;

        // Push all pending input requests onto the work queue.
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
  }

  /// @}

  /// @name Client API
  /// @{

  ValueType build(KeyType Key) {
    // Find the rule.
    auto it = RuleInfos.find(Key);
    if (it == RuleInfos.end()) {
      // FIXME: Error handling.
      std::cerr << "error: attempt to build unknown rule \"" << Key << "\"\n";
      exit(1);
    }
    auto& RuleInfo = it->second;

    // If we have already computed the result of this key, we are done.
    if (RuleInfo.IsComplete)
      return RuleInfo.Result;

    // Otherwise, start the task for this rule.
    beginRule(RuleInfo);

    // Run the build engine.
    executeTasks();

    // The task should now be complete.
    assert(TaskInfos.empty() && RuleInfo.PendingTaskInfo == nullptr &&
           RuleInfo.IsComplete);
    return RuleInfo.Result;
  }

  /// @}

  /// @name Task Management Client APIs
  /// @{

  Task* registerTask(Task* Task) {
    auto Result = TaskInfos.emplace(Task, TaskInfo(Task));
    assert(Result.second && "task already registered");
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

Task* BuildEngine::registerTask(Task* Task) {
  return static_cast<BuildEngineImpl*>(Impl)->registerTask(Task);
}

void BuildEngine::taskNeedsInput(Task* Task, KeyType Key, uintptr_t InputID) {
  return static_cast<BuildEngineImpl*>(Impl)->taskNeedsInput(Task, Key,
                                                             InputID);
}

