//===-- Engine.cpp --------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild3/Engine.h"

#include <llbuild3/Errors.hpp>

#include "llbuild3/ActionCache.h"
#include "llbuild3/ActionExecutor.h"
#include "llbuild3/CAS.h"
#include "llbuild3/Common.h"
#include "llbuild3/EngineInternal.pb.h"
#include "llbuild3/Label.h"
#include "llbuild3/support/LabelTrie.h"

#include "blake3.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace llbuild3;
using namespace llbuild3::internal;
using namespace llbuild3::support;

Task::~Task() {}
Rule::~Rule() {}
RuleProvider::~RuleProvider() {}

namespace llbuild3 {
namespace internal {

struct BuildContext {
  std::mutex buildMutex;
  std::optional<result<Artifact, Error>> buildResult;
  std::vector<std::function<void(result<Artifact, Error>)>> completionHandlers;
};

class IntTaskInterface {
private:
  TaskInterface& ti;

public:
  IntTaskInterface(TaskInterface& ti) : ti(ti) { }

  inline uint64_t taskID() { return ti.ctx; }
};

} // namespace internal
} // namespace llbuild3

#pragma mark - Engine implementation

namespace {

inline Error makeEngineError(EngineError code,
                             std::string desc = std::string()) {
  Error err;
  err.set_type(ErrorType::ENGINE);
  err.set_code(rawCode(code));
  if (!desc.empty()) {
    err.set_description(desc);
  }
  return err;
}

class EngineImpl: public ActionExecutorListener {
  struct TaskInfo;
  class RootTask;

  const EngineID engineID{generateEngineID()};

  const EngineConfig cfg;


  /// The CAS database
  std::shared_ptr<CASDatabase> casDB;

  /// The action cache.
  std::shared_ptr<ActionCache> actionCache;

  /// The action executor
  std::shared_ptr<ActionExecutor> actionExecutor;

  /// The logger
  std::shared_ptr<Logger> logger;
  LoggingContext logctx;

  std::mutex ruleProvidersMutex;
  std::vector<std::unique_ptr<RuleProvider>> ruleProviders;
  typedef std::vector<std::unique_ptr<RuleProvider>>::size_type ProviderID;
  LabelTrie<ProviderID> ruleProviderMap{true};
  LabelTrie<ProviderID> artifactProviderMap{true};

  std::mutex rulesMutex;
  std::vector<std::unique_ptr<Rule>> rules;
  typedef std::vector<std::unique_ptr<Rule>>::size_type RuleID;
  LabelTrie<RuleID> ruleRuleMap;
  LabelTrie<RuleID> ruleArtifactMap;

  std::mutex engineStateMutex;
  bool initialized = false;
  std::optional<uint64_t> initTask;
  uint64_t buildRequestCount = 0;

  /// The queue of input requests to process.
  struct TaskInputRequest {
    bool isRule;
    Label label;
    uint64_t sid;
  };

  struct PendingAction {
    ActionID execID;
    Action action;
    uint64_t sid;
    std::optional<result<ActionResult, Error>> result;
  };

  /// Information tracked for executing tasks.
  struct TaskInfo {
    TaskInfo(uint64_t id, std::unique_ptr<Task>&& task)
        : id(id), task(std::move(task)) {}

    uint64_t id;
    uint64_t minBuild;

    std::unique_ptr<Task> task;

    std::unordered_map<uint64_t, uint64_t> stableIDMap;

    std::unordered_map<uint64_t, TaskInputRequest> requestedTaskInputs;
    std::unordered_set<uint64_t> requestedBy;

    std::unordered_map<uint64_t, std::optional<SubtaskResult>> pendingSubtasks;
    std::unordered_map<uint64_t, PendingAction> pendingActions;

    std::unordered_set<uint64_t> waitingOn;

    TaskNextState nextState;
  };

  /// The tracked information for executing tasks.
  std::mutex taskInfosMutex;
  uint64_t nextTaskID = 0;
  std::unordered_map<uint64_t, TaskInfo> taskInfos;
  std::unordered_map<uint64_t, uint64_t> actionTaskMap;
  std::unordered_map<uint64_t, uint64_t> subtaskTaskMap;
  LabelTrie<uint64_t> taskNameMap;
  LabelTrie<uint64_t> taskArtifactMap;

  // Internal execution lanes
  unsigned numLanes = 1;
  std::vector<std::unique_ptr<std::thread>> lanes;

  struct JobDescriptor {
    uint64_t buildID = 0;
    uint64_t workID = 0;
  };
  struct JobContext {
    unsigned laneID;
  };

  struct Job {
    JobDescriptor desc;

    typedef std::function<void(const JobContext&)> work_fn_ty;
    work_fn_ty work;

    Job() {}
    Job(const JobDescriptor& desc, work_fn_ty work) : desc(desc), work(work) {}
  };

  struct JobComparator {
    bool operator()(const Job& lhs, const Job& rhs) const {
      return lhs.desc.buildID < rhs.desc.buildID &&
             lhs.desc.workID < rhs.desc.workID;
    }
  };

  std::mutex readyJobsMutex;
  std::priority_queue<Job, std::vector<Job>, JobComparator> readyJobs;
  std::condition_variable readyJobsCondition;
  bool shutdown{false};

public:
  EngineImpl(EngineConfig config, std::shared_ptr<CASDatabase> casDB,
             std::shared_ptr<ActionCache> cache,
             std::shared_ptr<ActionExecutor> executor,
             std::shared_ptr<Logger> logger,
             std::shared_ptr<ClientContext> clientContext)
    : cfg(config), casDB(casDB), actionCache(cache), actionExecutor(executor)
    , logger(logger)
  {
    // Ensure we always have a valid logger
    if (!this->logger) {
      this->logger = std::make_shared<NullLogger>();
    }
    logctx.engine = engineID;
    logctx.clientContext = clientContext;

    actionExecutor->attachListener(engineID, this);

    for (unsigned i = 0; i != numLanes; ++i) {
      lanes.push_back(std::unique_ptr<std::thread>(
          new std::thread(&EngineImpl::executeLane, this, i)));
    }
  }

  virtual ~EngineImpl() {
    // Shut down the lanes.
    {
      std::unique_lock<std::mutex> lock(readyJobsMutex);
      shutdown = true;
      readyJobsCondition.notify_all();
    }

    for (unsigned i = 0; i != numLanes; ++i) {
      lanes[i]->join();
    }

    actionExecutor->detachListener(engineID);
  }

  const EngineConfig& config() { return cfg; }

  std::optional<Error>
  registerRuleProvider(uint64_t taskID,
                       std::unique_ptr<RuleProvider>&& provider) {
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      assert(taskInfos.contains(taskID));
      auto& rTaskInfo = taskInfos.at(taskID);
      if (!rTaskInfo.task->props.init) {
        return makeEngineError(EngineError::TaskPropertyViolation,
                               "provider registration from non-initialization task");
      }
    }
    return internalRegisterRuleProvider(std::move(provider));
  }

  std::optional<Error>
  internalRegisterRuleProvider(std::unique_ptr<RuleProvider>&& provider) {
    std::lock_guard<std::mutex> lock(ruleProvidersMutex);

    auto rulePrefixes = provider->rulePrefixes();
    for (auto prefix : rulePrefixes) {
      // FIXME: check prefix validity
      if (ruleProviderMap.contains(prefix)) {
        // already registered prefix
        return makeEngineError(EngineError::DuplicateRuleProvider,
                               labelAsCanonicalString(prefix));
      }
    }

    auto artifactPrefixes = provider->artifactPrefixes();
    for (auto prefix : artifactPrefixes) {
      // FIXME: check prefix validity
      if (artifactProviderMap.contains(prefix)) {
        return makeEngineError(EngineError::DuplicateArtifactProvider,
                               labelAsCanonicalString(prefix));
      }
    }

    // No conflicts, store and register provider
    auto providerIdx = ruleProviders.size();
    ruleProviders.push_back(std::move(provider));

    for (auto prefix : rulePrefixes) {
      ruleProviderMap.insert({prefix, providerIdx});
    }
    for (auto prefix : artifactPrefixes) {
      artifactProviderMap.insert({prefix, providerIdx});
    }

    return {};
  }

  /// @name Client API
  /// @{

  Build build(const Label& artifact) {
    auto context = std::shared_ptr<BuildContext>(new BuildContext());

    uint64_t buildID = 0;
    {
      std::lock_guard<std::mutex> lock(engineStateMutex);
      buildID = ++buildRequestCount;
    }
    logger->event(logctx, {
      makeStat("log.message", "build_started"),
      makeStat("build_id", buildID)
    });
    std::unique_ptr<Task> task(new RootTask(*this, buildID, artifact, context));

    result<uint64_t, Error> res;
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      res = insertTask(buildID, std::move(task));
    }

    if (res.has_error()) {
      logger->event(logctx, {
        makeStat("log.message", "build_completed"),
        makeStat("build_id", buildID),
        makeStat("status", res.error())
      });
      std::lock_guard<std::mutex> lock(context->buildMutex);
      context->buildResult = fail(res.error());
    }

    return Build(context);
  }

  /// @}

  /// @name Task Management Client APIs
  /// @{

  std::shared_ptr<CASDatabase> cas() {
    return casDB;
  }

  result<uint64_t, Error> taskRequestArtifact(uint64_t taskID,
                                              const Label& label) {
    // FIXME: concretize label with context?

    uint64_t buildID = 0;

    // check if an active task is producing the artifact
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      auto entry = taskArtifactMap[label];
      if (entry.has_value()) {
        // task is already in flight, just return the id
        assert(taskInfos.contains(taskID));
        auto& ti = taskInfos.at(taskID);
        auto sid = stableHashTaskRequest(false, label);
        ti.requestedTaskInputs.insert({*entry, TaskInputRequest{false, label, sid}});
        ti.stableIDMap.insert({sid, *entry});
        return sid;
      }

      assert(taskInfos.contains(taskID));
      auto& rTaskInfo = taskInfos.at(taskID);
      buildID = rTaskInfo.minBuild;
    }

    // check if an active rule can produce the artifact
    Rule* rule = nullptr;
    {
      std::lock_guard<std::mutex> lock(rulesMutex);
      auto entry = ruleArtifactMap[label];
      if (entry.has_value()) {
        rule = rules[*entry].get();
      }
    }
    if (rule) {
      auto res = constructTask(buildID, rule, label);
      if (res.has_value()) {
        std::lock_guard<std::mutex> lock(taskInfosMutex);
        assert(taskInfos.contains(taskID));
        auto& ti = taskInfos.at(taskID);
        auto sid = stableHashTaskRequest(false, label);
        ti.requestedTaskInputs.insert({*res, TaskInputRequest{false, label, sid}});
        ti.stableIDMap.insert({sid, *res});
        return sid;
      }
      return res;
    }

    // check if a rule provider can provide a rule for the artifact
    RuleProvider* ruleProvider = nullptr;
    {
      std::lock_guard<std::mutex> lock(ruleProvidersMutex);
      auto entry = artifactProviderMap[label];
      if (entry.has_value()) {
        ruleProvider = ruleProviders[*entry].get();
      }
    }
    if (!ruleProvider) {
      return fail(makeEngineError(EngineError::NoArtifactProducer,
                                  labelAsCanonicalString(label)));
    }
    rule = nullptr;
    {
      std::lock_guard<std::mutex> lock(rulesMutex);
      auto ruleEntry = ruleArtifactMap[label];
      if (ruleEntry.has_value()) {
        // something constructed the rule in the meantime
        rule = rules[*ruleEntry].get();
      } else {
        auto newRule = ruleProvider->ruleForArtifact(label);
        if (!newRule) {
          return fail(makeEngineError(EngineError::RuleConstructionFailed,
                                      labelAsCanonicalString(label)));
        }
        rule = newRule.get();

        // try inserting rule
        auto newRuleRes = insertRule(std::move(newRule));
        if (newRuleRes.has_error()) {
          return newRuleRes;
        }
      }
    }
    if (rule) {
      auto res = constructTask(buildID, rule, label);
      if (res.has_value()) {
        std::lock_guard<std::mutex> lock(taskInfosMutex);
        assert(taskInfos.contains(taskID));
        auto& ti = taskInfos.at(taskID);
        auto sid = stableHashTaskRequest(false, label);
        ti.requestedTaskInputs.insert({*res, TaskInputRequest{false, label, sid}});
        ti.stableIDMap.insert({sid, *res});
        return sid;
      }
      return res;
    }

    return fail(makeEngineError(EngineError::NoArtifactProducer,
                                labelAsCanonicalString(label)));
  }

  result<uint64_t, Error> taskRequestRule(uint64_t taskID, const Label& label) {
    // FIXME: concretize label with context?

    uint64_t buildID = 0;

    // check if an active task exists for the rule
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      auto entry = taskNameMap[label];
      if (entry.has_value()) {
        // task is already in flight, just return the id
        return *entry;
      }

      assert(taskInfos.contains(taskID));
      auto& rTaskInfo = taskInfos.at(taskID);
      buildID = rTaskInfo.minBuild;
    }

    // check if an active rule exists
    Rule* rule = nullptr;
    {
      std::lock_guard<std::mutex> lock(rulesMutex);
      auto entry = ruleRuleMap[label];
      if (entry.has_value()) {
        rule = rules[*entry].get();
      }
    }
    if (rule) {
      auto res = constructTask(buildID, rule, label);
      if (res.has_value()) {
        std::lock_guard<std::mutex> lock(taskInfosMutex);
        assert(taskInfos.contains(taskID));
        auto& ti = taskInfos.at(taskID);
        auto sid = stableHashTaskRequest(true, label);
        ti.requestedTaskInputs.insert({*res, TaskInputRequest{true, label, sid}});
        ti.stableIDMap.insert({sid, *res});
        return sid;
      }
      return res;
    }

    // check if a rule provider can provide the rule
    RuleProvider* ruleProvider = nullptr;
    {
      std::lock_guard<std::mutex> lock(ruleProvidersMutex);
      auto entry = ruleProviderMap[label];
      if (entry.has_value()) {
        ruleProvider = ruleProviders[*entry].get();
      }
    }
    if (!ruleProvider) {
      return fail(makeEngineError(EngineError::NoProviderForRule,
                                  labelAsCanonicalString(label)));
    }
    rule = nullptr;
    {
      std::lock_guard<std::mutex> lock(rulesMutex);
      auto ruleEntry = ruleRuleMap[label];
      if (ruleEntry.has_value()) {
        // something constructed the rule in the meantime
        rule = rules[*ruleEntry].get();
      } else {
        auto newRule = ruleProvider->ruleByName(label);
        if (!newRule) {
          return fail(makeEngineError(EngineError::RuleConstructionFailed,
                                      labelAsCanonicalString(label)));
        }
        rule = newRule.get();

        // try inserting rule
        auto newRuleRes = insertRule(std::move(newRule));
        if (newRuleRes.has_error()) {
          return newRuleRes;
        }
      }
    }
    if (rule) {
      auto res = constructTask(buildID, rule, label);
      if (res.has_value()) {
        std::lock_guard<std::mutex> lock(taskInfosMutex);
        assert(taskInfos.contains(taskID));
        auto& ti = taskInfos.at(taskID);
        auto sid = stableHashTaskRequest(true, label);
        ti.requestedTaskInputs.insert({*res, TaskInputRequest{true, label, sid}});
        ti.stableIDMap.insert({sid, *res});
        return sid;
      }
      return res;
    }

    return fail(makeEngineError(EngineError::NoProviderForRule,
                                labelAsCanonicalString(label)));
  }

  result<uint64_t, Error> taskRequestAction(uint64_t taskID, const Action& action) {
    uint64_t buildID = 0;
    uint64_t workID = 0;

    // check if an active task exists
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      assert(taskInfos.contains(taskID));
      auto& rTaskInfo = taskInfos.at(taskID);
      buildID = rTaskInfo.minBuild;
      workID = nextTaskID++;
      actionTaskMap.insert({workID, taskID});
    }

    // submit the subtask for execution
    ClientActionID cid{buildID, workID};
    ActionRequest req{engineID, cid, ActionPriority::Default, action};
    auto res = actionExecutor->submit(req);

    // check that the subtask was accepted
    if (res.has_error()) {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      actionTaskMap.erase(workID);
      return fail(res.error());
    }

    // record the subtask as pending
    std::lock_guard<std::mutex> lock(taskInfosMutex);
    auto& rTaskInfo = taskInfos.at(taskID);
    auto sid = stableHashActionRequest(action);
    rTaskInfo.pendingActions.insert({workID, {*res, action, sid, {}}});
    rTaskInfo.stableIDMap.insert({sid, workID});

    return sid;
  }

  result<uint64_t, Error> taskSpawnSubtask(uint64_t taskID, const Subtask& subtask) {
    uint64_t buildID = 0;
    uint64_t workID = 0;
    uint64_t sid = 0;

    // check if an active task exists
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      assert(taskInfos.contains(taskID));
      auto& rTaskInfo = taskInfos.at(taskID);
      buildID = rTaskInfo.minBuild;
      workID = nextTaskID++;
      sid = stableHashSubtaskRequest(workID);
      subtaskTaskMap.insert({workID, taskID});
    }

    // submit the subtask for execution
    ClientActionID cid{buildID, workID};
    SubtaskRequest req{engineID, cid, ActionPriority::Default, subtask, SubtaskInterface(this, taskID)};
    auto res = actionExecutor->submit(req);

    // check that the subtask was accepted
    if (res.has_error()) {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      subtaskTaskMap.erase(workID);
      return fail(res.error());
    }

    // record the subtask as pending
    std::lock_guard<std::mutex> lock(taskInfosMutex);
    auto& rTaskInfo = taskInfos.at(taskID);
    rTaskInfo.pendingSubtasks.insert({workID, {}});
    rTaskInfo.stableIDMap.insert({sid, workID});

    return sid;
  }

  /// @}

  /// @name Task Management Client APIs
  /// @{

  void notifyActionStart(ClientActionID cid, ActionID aid) override {
    std::lock_guard<std::mutex> lock(taskInfosMutex);
    auto entry = actionTaskMap.find(cid.workID);
    if (entry == actionTaskMap.end()) {
      return;
    }

    auto taskID = entry->second;
    auto& rtask = taskInfos.at(taskID);
    rtask.pendingActions.at(cid.workID).execID = aid;
  }

  void notifyActionComplete(ClientActionID cid, result<ActionResult, Error> result) override {
    std::lock_guard<std::mutex> lock(taskInfosMutex);
    auto entry = actionTaskMap.find(cid.workID);
    if (entry == actionTaskMap.end()) {
      return;
    }

    auto taskID = entry->second;
    auto& rtask = taskInfos.at(taskID);
    rtask.pendingActions.at(cid.workID).result = {std::move(result)};
    bool wasBlocked = !rtask.waitingOn.empty();
    rtask.waitingOn.erase(cid.workID);

    if (wasBlocked && rtask.waitingOn.empty()) {
      // finished last blocked input, unblock task
      unblockTask(rtask);
    }
  }

  void notifySubtaskStart(ClientActionID) override {
    // no op
  }

  void notifySubtaskComplete(ClientActionID cid, SubtaskResult result) override {
    std::lock_guard<std::mutex> lock(taskInfosMutex);
    auto entry = subtaskTaskMap.find(cid.workID);
    if (entry == subtaskTaskMap.end()) {
      return;
    }

    auto taskID = entry->second;
    auto& rtask = taskInfos.at(taskID);
    rtask.pendingSubtasks.at(cid.workID) = std::move(result);
    bool wasBlocked = !rtask.waitingOn.empty();
    rtask.waitingOn.erase(cid.workID);

    if (wasBlocked && rtask.waitingOn.empty()) {
      // finished last blocked input, unblock task
      unblockTask(rtask);
    }
  }

  /// @}

private:
  static uint64_t stableHashTaskRequest(bool isRule, const Label& label) {
    blake3_hasher hasher;

    blake3_hasher_init(&hasher);

    if (isRule) {
      blake3_hasher_update(&hasher, "tr", 2);
    } else {
      blake3_hasher_update(&hasher, "ta", 2);
    }

    std::string lblstr;
    label.SerializeToString(&lblstr);
    blake3_hasher_update(&hasher, lblstr.data(), lblstr.size());

    std::array<uint8_t, sizeof(uint64_t)> buffer;
    blake3_hasher_finalize(&hasher, buffer.data(), buffer.size());

    uint64_t rval;
    std::memcpy(&rval, buffer.data(), sizeof(uint64_t));
    return rval;
  }

  static uint64_t stableHashActionRequest(const Action& action) {
    blake3_hasher hasher;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, "a", 1);

    std::string actstr;
    action.SerializeToString(&actstr);
    blake3_hasher_update(&hasher, actstr.data(), actstr.size());

    std::array<uint8_t, sizeof(uint64_t)> buffer;
    blake3_hasher_finalize(&hasher, buffer.data(), buffer.size());

    uint64_t rval;
    std::memcpy(&rval, buffer.data(), sizeof(uint64_t));
    return rval;
  }

  static uint64_t stableHashSubtaskRequest(const uint64_t subtaskID) {
    blake3_hasher hasher;

    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, "s", 1);
    blake3_hasher_update(&hasher, &subtaskID, sizeof(subtaskID));

    std::array<uint8_t, sizeof(uint64_t)> buffer;
    blake3_hasher_finalize(&hasher, buffer.data(), buffer.size());

    uint64_t rval;
    std::memcpy(&rval, buffer.data(), sizeof(uint64_t));
    return rval;
  }

  result<uint64_t, Error> constructTask(uint64_t buildID, Rule* rule,
                                        const Label& label) {
    std::lock_guard<std::mutex> lock(taskInfosMutex);
    auto taskEntry = taskArtifactMap[label];
    if (taskEntry.has_value()) {
      // something started this task in the meantime, just return it
      return *taskEntry;
    }

    // construct the task
    auto task = rule->configureTask();
    if (!task) {
      return fail(makeEngineError(EngineError::TaskConstructionFailed));
    }

    if (task->props.init) {
      std::lock_guard<std::mutex> lock(engineStateMutex);
      if (initialized) {
        return fail(makeEngineError(EngineError::InvalidEngineState,
                                    "init task after initialization"));
      }
    }

    return insertTask(buildID, std::move(task));
  }

  // must hold `rulesMutex` when calling
  result<uint64_t, Error> insertRule(std::unique_ptr<Rule>&& rule) {
    // Verify the rule
    if (ruleRuleMap.contains(rule->name())) {
      return fail(makeEngineError(EngineError::DuplicateRule,
                                  labelAsCanonicalString(rule->name())));
    }
    for (auto artifact : rule->produces()) {
      if (ruleArtifactMap.contains(artifact)) {
        // FIXME: add rule name to error description
        return fail(makeEngineError(EngineError::DuplicateRuleArtifact,
                                    labelAsCanonicalString(artifact)));
      }
    }

    // Insert the rule
    auto ruleID = rules.size();
    ruleRuleMap.insert({rule->name(), ruleID});
    for (auto artifact : rule->produces()) {
      ruleArtifactMap.insert({artifact, ruleID});
    }
    rules.push_back(std::move(rule));
    return ruleID;
  }

  // must hold `taskInfosMutex` when calling
  result<uint64_t, Error> insertTask(uint64_t buildID,
                                     std::unique_ptr<Task>&& task) {
    // Verify the task
    if (taskNameMap.contains(task->name())) {
      return fail(makeEngineError(EngineError::DuplicateTask,
                                  labelAsCanonicalString(task->name())));
    }
    for (auto artifact : task->produces()) {
      if (taskArtifactMap.contains(artifact)) {
        // FIXME: add task names to error description
        return fail(makeEngineError(EngineError::DuplicateTaskArtifact,
                                    labelAsCanonicalString(artifact)));
      }
    }

    // Insert the task
    auto taskID = nextTaskID++;
    auto insertion = taskInfos.try_emplace(taskID, taskID, std::move(task));
    auto& taskInfo = (insertion.first)->second;
    const auto& ptask = *taskInfo.task;

    taskInfo.minBuild = buildID;

    taskNameMap.insert({ptask.name(), taskID});
    for (auto artifact : ptask.produces()) {
      taskArtifactMap.insert({artifact, taskID});
    }

    enqueueReadyTask((insertion.first)->second, {}, {}, {});

    return taskID;
  }

  /// @name Build Execution
  /// @{

  void enqueueReadyTask(TaskInfo& taskInfo, const TaskContext& ctx,
                        const TaskInputs& inputs, const SubtaskResults& sres) {

    auto work = [this, ctx, inputs, sres, &taskInfo](const JobContext&) {
      TaskInterface ti{this, taskInfo.id};
      auto next = taskInfo.task->compute(ti, ctx, inputs, sres);

      updateTaskCache(taskInfo, ctx, inputs, next);
      processTaskNextState(taskInfo, next);
    };
    addJob(Job({taskInfo.minBuild, taskInfo.id}, work));
  }
  
  void checkTaskCacheForReadyTask(TaskInfo& taskInfo, const TaskContext& ctx,
                                  const TaskInputs& inputs,
                                  const SubtaskResults& sres) {
    // If no action cache, or if there are any pending subtasks, skip the check
    if (!actionCache || taskInfo.pendingSubtasks.size() > 0) {
      enqueueReadyTask(taskInfo, ctx, inputs, sres);
      return;
    }

    auto work = [this, ctx, inputs, sres, &taskInfo](const JobContext&) {
      // Set up transition key
      TaskTransitionKey key;
      *key.mutable_ctx() = ctx;
      *key.mutable_signature() = taskInfo.task->signature();
      *key.mutable_inputs() = inputs;

      CASObject keyObj;
      key.SerializeToString(keyObj.mutable_data());

      auto keyID = casDB->identify(keyObj);

      // calc cache key
      CacheKey cacheKey;
      *cacheKey.mutable_label() = taskInfo.task->name();
      cacheKey.set_type(CACHE_KEY_TYPE_TASK);
      *cacheKey.mutable_data() = keyID;

      actionCache->get(cacheKey, [this, ctx, inputs, sres, &taskInfo](result<CacheValue, Error> res) {
        if (res.has_error()) {
          logger->error(logctx, res.error());
          enqueueReadyTask(taskInfo, ctx, inputs, sres);
          return;
        }

        if (!res->has_data()) {
          // not found
          enqueueReadyTask(taskInfo, ctx, inputs, sres);
          return;
        }

        // load entry
        casDB->get(res->data(), [this, ctx, inputs, sres, &taskInfo](result<CASObject, Error> res) {
          if (res.has_error()) {
            logger->error(logctx, res.error());
            enqueueReadyTask(taskInfo, ctx, inputs, sres);
            return;
          }

          TaskTransitionValue value;
          if (!value.ParseFromString(res->data())) {
            logger->error(logctx,
              makeEngineError(EngineError::InternalProtobufSerialization,
                              "failed to parse cached task transition")
            );
            enqueueReadyTask(taskInfo, ctx, inputs, sres);
            return;
          }

          std::vector<const TaskRequest*> newreqs;
          {
            std::lock_guard<std::mutex> lock(taskInfosMutex);
            for (int i = 0; i < value.requests_size(); i++) {
              auto sid = value.requests(i).id();
              if (taskInfo.stableIDMap.find(sid) == taskInfo.stableIDMap.end()) {
                // not found, need to initiate the request
                newreqs.push_back(&value.requests(i));
              }
            }
          }

          for (auto req : newreqs) {
            result<uint64_t, Error> res;
            switch (req->details_case()) {
              case TaskRequest::kArtifact:
                res = taskRequestArtifact(taskInfo.id, req->artifact().label());
                break;
              case TaskRequest::kRule:
                res = taskRequestRule(taskInfo.id, req->rule().label());
                break;
              case TaskRequest::kAction:
                res = taskRequestAction(taskInfo.id, req->action());
                break;
              default:
                res = fail(makeEngineError(EngineError::InternalProtobufSerialization,
                                           "unsupported task request type in cached value"));
                break;
            }

            if (res.has_error()) {
              logger->error(logctx, res.error());
              enqueueReadyTask(taskInfo, ctx, inputs, sres);
              return;
            }
          }

          processTaskNextState(taskInfo, value.state());
        });
      });

    };
    addJob(Job({taskInfo.minBuild, taskInfo.id}, work));
  }

  void updateTaskCache(TaskInfo& taskInfo, const TaskContext& ctx, const TaskInputs& inputs, const TaskNextState& next) {
    // Skip if we don't have a cache
    if (!actionCache)
      return;

    // Skip if this task is not cacheable
    if (!taskInfo.task->props.cacheable)
      return;

    // Skip if this task has outstanding subtasks
    if (taskInfo.pendingSubtasks.size() > 0)
      return;

    // Set up transition key
    TaskTransitionKey key;
    *key.mutable_ctx() = ctx;
    *key.mutable_signature() = taskInfo.task->signature();
    *key.mutable_inputs() = inputs;

    CASObject keyObj;
    key.SerializeToString(keyObj.mutable_data());

    // Set up transition value
    TaskTransitionValue value;
    *value.mutable_state() = next;
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);

      for (auto& req : taskInfo.requestedTaskInputs) {
        auto& tr = *value.add_requests();
        tr.set_id(req.second.sid);
        if (req.second.isRule) {
          *tr.mutable_rule()->mutable_label() = req.second.label;
        } else {
          *tr.mutable_artifact()->mutable_label() = req.second.label;
        }
      }

      for (auto& act : taskInfo.pendingActions) {
        auto& tr = *value.add_requests();
        tr.set_id(act.second.sid);
        *tr.mutable_action() = act.second.action;
      }
    }

    CASObject valueObj;
    value.SerializeToString(valueObj.mutable_data());

    // Store both in CAS
    casDB->put(keyObj, [this, name=taskInfo.task->name(), valueObj](result<CASID, Error> res) {
      if (res.has_error()) {
        // caching is best effort, on failure just continue
        logger->error(logctx, res.error());
        return;
      }

      auto keyID = *res;
      casDB->put(valueObj, [this, name, keyID](result<CASID, Error> res) {
        if (res.has_error()) {
          // caching is best effort, on failure just continue
          logger->error(logctx, res.error());
          return;
        }
        CacheKey cacheKey;
        *cacheKey.mutable_label() = name;
        cacheKey.set_type(CACHE_KEY_TYPE_TASK);
        *cacheKey.mutable_data() = keyID;

        CacheValue cacheValue;
        *cacheValue.mutable_data() = *res;

        // FIXME: record action stats

        actionCache->update(cacheKey, cacheValue);
      });
    });
  }

  void processTaskNextState(TaskInfo& taskInfo, const TaskNextState& next) {
    std::lock_guard<std::mutex> lock(taskInfosMutex);

    switch (next.StateValue_case()) {
    case TaskNextState::kWait: {
      auto batchState = next.wait();

      bool badInput = false;
      uint64_t badID = 0;
      std::unordered_set<uint64_t> unresolvedInputs;
      for (auto sid : batchState.ids()) {
        auto idp = taskInfo.stableIDMap.find(sid);
        if (idp == taskInfo.stableIDMap.end()) {
          badInput = true;
          badID = sid;
          break;
        }
        auto id = idp->second;
        if (taskInfo.requestedTaskInputs.contains(id)) {
          auto& inputTask = taskInfos.at(id);

          if (!inputTask.nextState.has_result() &&
              !inputTask.nextState.has_error()) {
            unresolvedInputs.insert(id);
            inputTask.requestedBy.insert(taskInfo.id);
          }
        } else if (taskInfo.pendingActions.contains(id)) {
          if (!taskInfo.pendingActions.at(id).result.has_value()) {
            unresolvedInputs.insert(id);
          }
        } else if (taskInfo.pendingSubtasks.contains(id)) {
          if (!taskInfo.pendingSubtasks.at(id).has_value()) {
            unresolvedInputs.insert(id);
          }
        } else {
          badInput = true;
          badID = sid;
          break;
        }
      }
      if (badInput) {
        // invalid input requested, transition to error state
        auto err = taskInfo.nextState.mutable_error();
        err->set_type(ErrorType::ENGINE);
        err->set_code(rawCode(EngineError::UnrequestedInput));
        err->set_description("input id " + std::to_string(badID));

        processFinishedTask(taskInfo);
        break;
      }

      taskInfo.nextState = next;

      if (!unresolvedInputs.empty()) {
        taskInfo.waitingOn = unresolvedInputs;
      } else {
        unblockTask(taskInfo);
      }
      break;
    }

    case TaskNextState::kResult:
    case TaskNextState::kError: {
      taskInfo.nextState = next;
      processFinishedTask(taskInfo);
      break;
    }

    default: {
      auto err = taskInfo.nextState.mutable_error();
      err->set_type(ErrorType::ENGINE);
      err->set_code(rawCode(EngineError::InvalidNextState));

      processFinishedTask(taskInfo);
      break;
    }
    }
  }

  void processFinishedTask(TaskInfo& taskInfo) {
    // update all tasks waiting on this task
    auto work = [this, &taskInfo](const JobContext&) {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      for (auto id : taskInfo.requestedBy) {
        auto& rtask = taskInfos.at(id);

        // FIXME: This is leaving tasks resident indefinitely
        // FIXME: Should probably mark the complete result instead, and
        // FIXME: collect it from the action cache?

        bool wasBlocked = !rtask.waitingOn.empty();
        rtask.waitingOn.erase(taskInfo.id);
        if (wasBlocked && rtask.waitingOn.empty()) {
          // finished last blocked input, unblock task
          unblockTask(rtask);
        }
      }
    };
    addJob(Job({taskInfo.minBuild, taskInfo.id}, work));
  }

  void unblockTask(TaskInfo& taskInfo) {
    if (taskInfo.nextState.has_wait()) {
      auto batchState = taskInfo.nextState.wait();

      TaskInputs inputs;
      SubtaskResults sres;

      for (auto sid : batchState.ids()) {
        auto idp = taskInfo.stableIDMap.find(sid);
        if (idp == taskInfo.stableIDMap.end()) {
          // bad stable ID?
          auto err = taskInfo.nextState.mutable_error();
          err->set_type(ErrorType::ENGINE);
          err->set_code(rawCode(EngineError::InternalInconsistency));
          err->set_description("stable ID not found " + std::to_string(sid));

          processFinishedTask(taskInfo);
          return;
        }
        auto id = idp->second;
        taskInfo.stableIDMap.erase(idp);
        if (auto it = taskInfos.find(id); it != taskInfos.end()) {
          auto& inputTask = it->second;
          auto input = inputs.add_inputs();
          auto& req = taskInfo.requestedTaskInputs.at(id);
          prepareInput(input, inputTask, req);
          taskInfo.requestedTaskInputs.erase(id);
        } else if (auto it = taskInfo.pendingActions.find(id);
                   it != taskInfo.pendingActions.end()) {
          auto& inputAction = it->second;
          auto input = inputs.add_inputs();
          prepareActionResult(input, inputAction.sid, *inputAction.result);
          taskInfo.pendingActions.erase(it);
        } else if (auto it = taskInfo.pendingSubtasks.find(id);
                   it != taskInfo.pendingSubtasks.end()) {
          assert(it->second.has_value());
          sres.insert({sid, it->second.value()});
          taskInfo.pendingSubtasks.erase(it);
        } else {
          // bad input
          auto err = taskInfo.nextState.mutable_error();
          err->set_type(ErrorType::ENGINE);
          err->set_code(rawCode(EngineError::InternalInconsistency));
          err->set_description("input not found " + std::to_string(id));

          processFinishedTask(taskInfo);
          return;
        }
      }

      checkTaskCacheForReadyTask(taskInfo, batchState.context(), inputs, sres);
    } else {
      auto err = taskInfo.nextState.mutable_error();
      err->set_type(ErrorType::ENGINE);
      err->set_code(rawCode(EngineError::InvalidNextState));

      processFinishedTask(taskInfo);
    }
  }

  void prepareInput(TaskInput* input, const TaskInfo& taskInfo,
                    const TaskInputRequest& req) const {
    input->set_id(req.sid);
    if (taskInfo.nextState.has_result()) {
      if (req.isRule) {
        *input->mutable_result() = taskInfo.nextState.result();
      } else {
        // Find the requested artifact
        auto& res = taskInfo.nextState.result();
        for (auto art : res.artifacts()) {
          std::string artLbl, reqLbl;
          art.label().SerializeToString(&artLbl);
          req.label.SerializeToString(&reqLbl);
          if (artLbl == reqLbl) {
            *input->mutable_artifact() = art;
            return;
          }
        }
        *input->mutable_error() = makeEngineError(EngineError::InternalInconsistency,
                                                  "requested input not found");
      }
    } else {
      *input->mutable_error() = taskInfo.nextState.error();
    }
  }

  void prepareActionResult(TaskInput* input, uint64_t sid,
                           const result<ActionResult, Error>& res) const {
    input->set_id(sid);
    if (res.has_value()) {
      *input->mutable_action() = *res;
    } else {
      *input->mutable_error() = res.error();
    }
  }

  /// @name Init Task
  /// @{

  class InitTask : public Task {
    EngineImpl& engine;
    Label requestName;
    Signature taskSignature;

  public:
    InitTask(EngineImpl& engine)
        : Task(Task::Properties(true, false)), engine(engine) {
      requestName.add_components("__init__");
    }

    const Label& name() const { return requestName; }
    const Signature& signature() const { return taskSignature; }

    std::vector<Label> produces() const { return {requestName}; }

    TaskNextState compute(TaskInterface ti, const TaskContext& ctx,
                          const TaskInputs& inputs, const SubtaskResults&) {
      if (ctx.has_int_state()) {
        std::lock_guard<std::mutex> lock(engine.engineStateMutex);
        engine.initialized = true;

        TaskNextState next;
        if (inputs.inputs(0).has_error()) {
          *(next.mutable_error()) = inputs.inputs(0).error();
        } else {
          next.mutable_result();
        }
        return next;
      }

      if (!engine.cfg.initRule.has_value()) {
        // No initialization rule configured, return success
        std::lock_guard<std::mutex> lock(engine.engineStateMutex);
        engine.initialized = true;
        TaskNextState next;
        next.mutable_result();
        return next;
      }

      auto ares = ti.requestRule(*engine.cfg.initRule);
      if (ares.has_error()) {
        TaskNextState next;
        *(next.mutable_error()) = ares.error();
        return next;
      }

      TaskNextState next;
      next.mutable_wait()->add_ids(*ares);
      next.mutable_wait()->mutable_context()->set_int_state(1);
      return next;
    }
  };

  /// @}

  /// @name Root Task
  /// @{

  class RootTask : public Task {
    EngineImpl& engine;
    uint64_t buildID;
    Label requestName;
    Label artifact;
    Signature taskSignature;
    std::shared_ptr<BuildContext> buildContext;

    enum class State: int64_t {
      Start = 0,
      Initialization = 1,
      Artifact = 2
    };
    constexpr std::underlying_type_t<State> raw(State state) {
      return static_cast<std::underlying_type_t<State>>(state);
    }

  public:
    RootTask(EngineImpl& engine, uint64_t requestID, const Label& artifact,
             std::shared_ptr<BuildContext> buildContext)
        : Task(Task::Properties(false, false)), engine(engine), buildID(requestID), artifact(artifact), buildContext(buildContext) {
      requestName.add_components("__build__");
      requestName.add_components(std::to_string(requestID));
    }

    const Label& name() const { return requestName; }
    const Signature& signature() const { return taskSignature; }

    std::vector<Label> produces() const { return {requestName}; }

    TaskNextState compute(TaskInterface ti, const TaskContext& ctx,
                          const TaskInputs& inputs, const SubtaskResults&) {
      State state = State::Start;
      if (ctx.has_int_state()) {
        state = static_cast<State>(ctx.int_state());
      }

      switch (state) {
        case State::Start:
          return performInit(ti);
        case State::Initialization:
          return checkInit(ti, inputs);
        case State::Artifact:
          return processArtifact(inputs);
        default:
          sendResult(fail(makeEngineError(EngineError::InternalInconsistency,
                                          "invalid root task state")));
          TaskNextState next;
          next.mutable_result();
          return next;
      }
    }

  private:
    TaskNextState performInit(TaskInterface ti) {
      auto taskID = IntTaskInterface(ti).taskID();
      std::lock_guard<std::mutex> lock(engine.engineStateMutex);
      if (!engine.initialized) {
        Label initlbl;
        initlbl.add_components("__init__");
        auto sid = stableHashTaskRequest(true, initlbl);

        if (engine.initTask.has_value()) {
          auto initTaskID = *engine.initTask;
          std::lock_guard<std::mutex> lock(engine.taskInfosMutex);
          auto& ti = engine.taskInfos.at(taskID);
          ti.requestedTaskInputs.insert({initTaskID, TaskInputRequest{true, initlbl, sid}});
          ti.stableIDMap.insert({sid, initTaskID});
        } else {
          std::unique_ptr<Task> task(new InitTask(engine));
          result<uint64_t, Error> res;
          {
            std::lock_guard<std::mutex> lock(engine.taskInfosMutex);
            res = engine.insertTask(1, std::move(task));
            if (res) {
              auto& ti = engine.taskInfos.at(taskID);
              ti.requestedTaskInputs.insert({*res, TaskInputRequest{true, initlbl, sid}});
              ti.stableIDMap.insert({sid, *res});
              engine.initTask = *res;
            }
          }

          if (res.has_error()) {
            sendResult(fail(res.error()));

            TaskNextState next;
            next.mutable_result();
            return next;
          }
        }

        TaskNextState next;
        next.mutable_wait()->add_ids(sid);
        next.mutable_wait()->mutable_context()->set_int_state(raw(State::Initialization));
        return next;

      } else {
        return requestArtifact(ti);
      }
    }

    TaskNextState checkInit(TaskInterface ti, const TaskInputs& inputs) {
      if (inputs.inputs_size() != 1) {
        sendResult(fail(makeEngineError(EngineError::InternalInconsistency,
                                        "unexpected initialization inputs")));
        TaskNextState next;
        next.mutable_result();
        return next;
      }
      auto initRes = inputs.inputs(0);
      if (initRes.has_error()) {
        sendResult(fail(initRes.error()));
        
        TaskNextState next;
        next.mutable_result();
        return next;
      }

      return requestArtifact(ti);
    }

    TaskNextState requestArtifact(TaskInterface ti) {
      auto ares = ti.requestArtifact(artifact);
      if (ares.has_error()) {
        sendResult(fail(ares.error()));

        TaskNextState next;
        next.mutable_result();
        return next;
      }

      TaskNextState next;
      next.mutable_wait()->add_ids(*ares);
      next.mutable_wait()->mutable_context()->set_int_state(raw(State::Artifact));
      return next;
    }

    TaskNextState processArtifact(const TaskInputs& inputs) {
      if (inputs.inputs_size() != 1) {
        sendResult(fail(makeEngineError(EngineError::InternalInconsistency,
                                        "invalid root task state")));
      }
      if (inputs.inputs(0).has_error()) {
        sendResult(fail(inputs.inputs(0).error()));
      } else if (!inputs.inputs(0).has_artifact()) {
        sendResult(fail(makeEngineError(EngineError::InternalInconsistency,
                                        "invalid root task input type")));
      } else {
        sendResult(inputs.inputs(0).artifact());
      }

      TaskNextState next;
      next.mutable_result();
      return next;
    }


    void sendResult(const result<Artifact, Error>& buildResult) {
      engine.logger->event(engine.logctx, {
        makeStat("log.message", "build_completed"),
        makeStat("build_id", buildID),
        buildResult.has_error() ? makeStat("status", buildResult.error()) : makeStat("status", "success")

      });
      std::lock_guard<std::mutex> lock(buildContext->buildMutex);

      buildContext->buildResult = buildResult;

      for (auto handler : buildContext->completionHandlers) {
        handler(buildResult);
      }

      buildContext->completionHandlers.clear();
    }
  };

  /// @}

  void executeLane(uint32_t laneNumber) {
    // Set the thread name, if available.
    std::string threadName = "llbuild3-engine-" + std::to_string(laneNumber);
#if defined(__APPLE__)
    pthread_setname_np(threadName.c_str());
#elif defined(__linux__)
    pthread_setname_np(threadName.c_str());
#endif

    while (true) {
      Job job{};
      {
        std::unique_lock<std::mutex> lock(readyJobsMutex);

        // While the queue is empty, wait for an item.
        while (!shutdown && readyJobs.empty()) {
          readyJobsCondition.wait(lock);
        }
        if (shutdown && readyJobs.empty()) {
          return;
        }

        job = readyJobs.top();
        readyJobs.pop();
      }

      // If we got an empty job, the queue is shutting down.
      if (job.desc.buildID == 0)
        break;

      // Process the job.
      JobContext context{laneNumber};
      { job.work(context); }
    }
  }

  void addJob(Job job) {
    std::lock_guard<std::mutex> guard(readyJobsMutex);
    readyJobs.push(job);
    readyJobsCondition.notify_one();
  }
};

} // namespace

#pragma mark - TaskInterface

std::optional<Error>
TaskInterface::registerRuleProvider(std::unique_ptr<RuleProvider>&& provider) {
  return static_cast<EngineImpl*>(impl)->registerRuleProvider(ctx,
      std::move(provider));
}

result<uint64_t, Error> TaskInterface::requestArtifact(const Label& label) {
  return static_cast<EngineImpl*>(impl)->taskRequestArtifact(ctx, label);
}

result<uint64_t, Error> TaskInterface::requestRule(const Label& label) {
  return static_cast<EngineImpl*>(impl)->taskRequestRule(ctx, label);
}

result<uint64_t, Error> TaskInterface::requestAction(const Action& action) {
  return static_cast<EngineImpl*>(impl)->taskRequestAction(ctx, action);
}

result<uint64_t, Error> TaskInterface::spawnSubtask(const Subtask& subtask) {
  return static_cast<EngineImpl*>(impl)->taskSpawnSubtask(ctx, subtask);
}

#pragma mark - SubtaskInterface

std::shared_ptr<CASDatabase> SubtaskInterface::cas() {
  return static_cast<EngineImpl*>(impl)->cas();
}


#pragma mark - Build

void Build::cancel() {
  // FIXME: handle cancellation
}

void Build::addCompletionHandler(
    std::function<void(result<Artifact, Error>)> handler) {
  std::lock_guard<std::mutex> lock(impl->buildMutex);

  if (impl->buildResult.has_value()) {
    handler(*impl->buildResult);
    return;
  }

  impl->completionHandlers.push_back(handler);
}

#pragma mark - Engine

Engine::Engine(EngineConfig config, std::shared_ptr<CASDatabase> casDB,
               std::shared_ptr<ActionCache> cache,
               std::shared_ptr<ActionExecutor> executor,
               std::shared_ptr<Logger> logger,
               std::shared_ptr<ClientContext> clientContext,
               std::unique_ptr<RuleProvider>&& init)
    : impl(new EngineImpl(config, casDB, cache, executor, logger, clientContext)) {
  static_cast<EngineImpl*>(impl)->internalRegisterRuleProvider(std::move(init));
}

Engine::~Engine() { delete static_cast<EngineImpl*>(impl); }

const EngineConfig& Engine::config() {
  return static_cast<EngineImpl*>(impl)->config();
}

std::shared_ptr<CASDatabase> Engine::cas() {
  return static_cast<EngineImpl*>(impl)->cas();
}

Build Engine::build(const Label& artifact) {
  return static_cast<EngineImpl*>(impl)->build(artifact);
}
