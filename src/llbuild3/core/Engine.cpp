//===-- Engine.cpp --------------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild3/core/Engine.h"

#include <llbuild3/Errors.hpp>

#include "llbuild3/core/ActionCache.h"
#include "llbuild3/core/CAS.h"
#include "llbuild3/core/EngineInternal.pb.h"
#include "llbuild3/core/Label.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
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
using namespace llbuild3::core;
using namespace llbuild3::core::internal;

Task::~Task() {}
Rule::~Rule() {}
RuleProvider::~RuleProvider() {}

namespace llbuild3 {
namespace core {
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
} // namespace core
} // namespace llbuild3

namespace {

template <typename entry_type> class LabelTrie {
private:
  bool prefixOnly;

  struct node;
  typedef std::unordered_map<std::string, node> node_map;
  struct node {
    std::variant<node_map, entry_type> contents;
  };

  node_map root;

public:
  LabelTrie(bool prefixOnly = false) : prefixOnly(prefixOnly) {}
  ~LabelTrie() {}

  bool insert(std::pair<const Label&, entry_type> entry);

  std::optional<entry_type> operator[](const Label& label) const;

  bool contains(const Label& label) const { return (*this)[label].has_value(); }
};

} // namespace

#pragma mark - Engine implementation

namespace {

inline Error makeEngineError(EngineError code,
                             std::string desc = std::string()) {
  Error err;
  err.set_type(ErrorType::ENGINE);
  err.set_code(code);
  if (!desc.empty()) {
    err.set_description(desc);
  }
  return err;
}

class EngineImpl {
  struct TaskInfo;
  class RootTask;

  const EngineConfig cfg;

  /// The CAS database
  std::shared_ptr<CASDatabase> casDB;

  /// The action cache.
  std::shared_ptr<ActionCache> actionCache;

  /// The execution queue reference; this is only valid while a build is
  /// actually in progress.
  //    std::unique_ptr<ExecutionQueue> executionQueue;

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
  };

  /// Information tracked for executing tasks.
  struct TaskInfo {
    TaskInfo(uint64_t id, std::unique_ptr<Task>&& task)
        : id(id), task(std::move(task)) {}

    uint64_t id;
    uint64_t minBuild;

    std::unique_ptr<Task> task;

    std::unordered_map<uint64_t, TaskInputRequest> requestedTaskInputs;
    std::unordered_set<uint64_t> requestedBy;

    /// The number of outstanding inputs that this task is waiting on to be
    /// provided.
    unsigned waitCount = 0;

    TaskNextState nextState;
  };

  /// The tracked information for executing tasks.
  std::mutex taskInfosMutex;
  uint64_t nextTaskID = 0;
  std::unordered_map<uint64_t, TaskInfo> taskInfos;
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
             std::shared_ptr<ActionCache> cache)
      : cfg(config), casDB(casDB), actionCache(cache) {

    for (unsigned i = 0; i != numLanes; ++i) {
      lanes.push_back(std::unique_ptr<std::thread>(
          new std::thread(&EngineImpl::executeLane, this, i)));
    }
  }

  ~EngineImpl() {
    // Shut down the lanes.
    {
      std::unique_lock<std::mutex> lock(readyJobsMutex);
      shutdown = true;
      readyJobsCondition.notify_all();
    }

    for (unsigned i = 0; i != numLanes; ++i) {
      lanes[i]->join();
    }
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
    std::unique_ptr<Task> task(new RootTask(*this, buildID, artifact, context));

    result<uint64_t, Error> res;
    {
      std::lock_guard<std::mutex> lock(taskInfosMutex);
      res = insertTask(buildID, std::move(task));
    }

    if (res.has_error()) {
      std::lock_guard<std::mutex> lock(context->buildMutex);
      context->buildResult = fail(res.error());
    }

    return Build(context);
  }

  /// @}

  /// @name Task Management Client APIs
  /// @{

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
        taskInfos.at(taskID).requestedTaskInputs.insert(
            {*entry, TaskInputRequest{false, label}});
        return *entry;
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
        taskInfos.at(taskID).requestedTaskInputs.insert(
            {*res, TaskInputRequest{false, label}});
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
        taskInfos.at(taskID).requestedTaskInputs.insert(
            {*res, TaskInputRequest{false, label}});
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
        taskInfos.at(taskID).requestedTaskInputs.insert(
            {*res, TaskInputRequest{true, label}});
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
        taskInfos.at(taskID).requestedTaskInputs.insert(
            {*res, TaskInputRequest{true, label}});
      }
      return res;
    }

    return fail(makeEngineError(EngineError::NoProviderForRule,
                                labelAsCanonicalString(label)));
  }

  result<uint64_t, Error> taskRequestAction(uint64_t taskID) {
    return fail(makeEngineError(EngineError::Unimplemented));
  }

  /// @}

private:
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

    TaskContext ctx;
    TaskInputs inputs;
    enqueueReadyTask((insertion.first)->second, ctx, inputs);

    return taskID;
  }

  /// @name Build Execution
  /// @{

  void enqueueReadyTask(TaskInfo& taskInfo, const TaskContext& ctx,
                        const TaskInputs& inputs) {

    auto work = [this, ctx, inputs, &taskInfo](const JobContext&) {
      TaskInterface ti{this, taskInfo.id};
      auto next = taskInfo.task->compute(ti, ctx, inputs);

      updateTaskCache(taskInfo, ctx, inputs, next);
      processTaskNextState(taskInfo, next);
    };
    addJob(Job({taskInfo.minBuild, taskInfo.id}, work));
  }
  
  void checkTaskCacheForReadyTask(TaskInfo& taskInfo, const TaskContext& ctx,
                                  const TaskInputs& inputs) {
    if (!actionCache) {
      enqueueReadyTask(taskInfo, ctx, inputs);
      return;
    }

    auto work = [this, ctx, inputs, &taskInfo](const JobContext&) {
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
      cacheKey.set_type(TASK);
      *cacheKey.mutable_content() = keyID;

      std::string foo = labelAsCanonicalString(cacheKey.label());

      actionCache->get(cacheKey, [this, ctx, inputs, &taskInfo, foo](result<CacheValue, Error> res) {
        if (res.has_error()) {
          // FIXME: log error
          enqueueReadyTask(taskInfo, ctx, inputs);
          return;
        }

        if (!res->has_data()) {
          // not found
          enqueueReadyTask(taskInfo, ctx, inputs);
          return;
        }

        // load entry
        casDB->get(res->data(), [this, ctx, inputs, &taskInfo](result<CASObject, Error> res) {
          if (res.has_error()) {
            // FIXME: log error
            enqueueReadyTask(taskInfo, ctx, inputs);
            return;
          }

          TaskTransitionValue value;
          if (!value.ParseFromString(res->data())) {
            // FIXME: log error
            enqueueReadyTask(taskInfo, ctx, inputs);
            return;
          }

          // FIXME: handle initiated async requests

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

    auto work = [this, ctx, inputs, &taskInfo, next](const JobContext&) {
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
      // FIXME: handle newly initiated requests

      CASObject valueObj;
      value.SerializeToString(valueObj.mutable_data());

      // Store both in CAS
      casDB->put(keyObj, [this, &taskInfo, valueObj](result<CASObjectID, Error> res) {
        if (res.has_error()) {
          // caching is best effort, on failure just continue
          // FIXME: report error
          return;
        }

        auto keyID = *res;
        casDB->put(valueObj, [this, &taskInfo, keyID](result<CASObjectID, Error> res) {
          if (res.has_error()) {
            // caching is best effort, on failure just continue
            // FIXME: report error
            return;
          }
          CacheKey cacheKey;
          *cacheKey.mutable_label() = taskInfo.task->name();
          cacheKey.set_type(TASK);
          *cacheKey.mutable_content() = keyID;

          CacheValue cacheValue;
          *cacheValue.mutable_data() = *res;

          // FIXME: record action stats

          actionCache->update(cacheKey, cacheValue);
        });
      });
    };
    addJob(Job({taskInfo.minBuild, taskInfo.id}, work));
  }

  void processTaskNextState(TaskInfo& taskInfo, const TaskNextState& next) {
    std::lock_guard<std::mutex> lock(taskInfosMutex);

    switch (next.StateValue_case()) {
    case TaskNextState::kWait: {
      auto batchState = next.wait();

      bool badInput = false;
      uint64_t badID = 0;
      int unresolvedInputs = 0;
      for (auto id : batchState.ids()) {
        if (!taskInfo.requestedTaskInputs.contains(id)) {
          badInput = true;
          badID = id;
          break;
        }

        auto& inputTask = taskInfos.at(id);

        if (!inputTask.nextState.has_result() &&
            !inputTask.nextState.has_error()) {
          ++unresolvedInputs;
          inputTask.requestedBy.insert(taskInfo.id);
        }
      }
      if (badInput) {
        // invalid input requested, transition to error state
        auto err = taskInfo.nextState.mutable_error();
        err->set_type(ErrorType::ENGINE);
        err->set_code(EngineError::UnrequestedInput);
        err->set_description("input id " + std::to_string(badID));

        processFinishedTask(taskInfo);
        break;
      }

      taskInfo.nextState = next;

      if (unresolvedInputs) {
        taskInfo.waitCount = unresolvedInputs;
      } else {
        TaskInputs inputs;

        for (auto id : batchState.ids()) {
          auto& inputTask = taskInfos.at(id);
          auto input = inputs.add_inputs();
          auto& req = taskInfo.requestedTaskInputs.at(id);
          prepareInput(input, inputTask, req);
        }

        checkTaskCacheForReadyTask(taskInfo, batchState.context(), inputs);
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
      err->set_code(EngineError::InvalidNextState);

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

        if (--rtask.waitCount == 0) {
          // finished last blocked input, unblock task
          if (rtask.nextState.has_wait()) {
            auto batchState = rtask.nextState.wait();

            TaskInputs inputs;

            for (auto id : batchState.ids()) {
              auto& inputTask = taskInfos.at(id);
              auto input = inputs.add_inputs();
              auto& req = rtask.requestedTaskInputs.at(id);
              prepareInput(input, inputTask, req);
            }

            checkTaskCacheForReadyTask(rtask, batchState.context(), inputs);
          } else {
            auto err = rtask.nextState.mutable_error();
            err->set_type(ErrorType::ENGINE);
            err->set_code(EngineError::InvalidNextState);

            processFinishedTask(rtask);
          }
        }
      }
    };
    addJob(Job({taskInfo.minBuild, taskInfo.id}, work));
  }

  void prepareInput(TaskInput* input, const TaskInfo& taskInfo,
                    const TaskInputRequest& req) const {
    input->set_id(taskInfo.id);
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
                          const TaskInputs& inputs) {
      if (ctx.has_intstate()) {
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
      next.mutable_wait()->mutable_context()->set_intstate(1);
      return next;
    }
  };

  /// @}

  /// @name Root Task
  /// @{

  class RootTask : public Task {
    EngineImpl& engine;
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
        : Task(Task::Properties(false, false)), engine(engine), artifact(artifact), buildContext(buildContext) {
      requestName.add_components("__build__");
      requestName.add_components(std::to_string(requestID));
    }

    const Label& name() const { return requestName; }
    const Signature& signature() const { return taskSignature; }

    std::vector<Label> produces() const { return {requestName}; }

    TaskNextState compute(TaskInterface ti, const TaskContext& ctx,
                          const TaskInputs& inputs) {
      State state = State::Start;
      if (ctx.has_intstate()) {
        state = static_cast<State>(ctx.intstate());
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
        uint64_t initTaskID;
        Label initlbl;
        initlbl.add_components("__init__");
        if (engine.initTask.has_value()) {
          initTaskID = *engine.initTask;
          std::lock_guard<std::mutex> lock(engine.taskInfosMutex);
          engine.taskInfos.at(taskID).requestedTaskInputs.insert(
              {initTaskID, TaskInputRequest{true, initlbl}});
        } else {
          std::unique_ptr<Task> task(new InitTask(engine));
          result<uint64_t, Error> res;
          {
            std::lock_guard<std::mutex> lock(engine.taskInfosMutex);
            res = engine.insertTask(1, std::move(task));
            if (res) {
              engine.taskInfos.at(taskID).requestedTaskInputs.insert(
                  {*res, TaskInputRequest{true, initlbl}});
            }
          }

          if (res.has_error()) {
            sendResult(fail(res.error()));

            TaskNextState next;
            next.mutable_result();
            return next;
          }
          initTaskID = *res;
        }

        TaskNextState next;
        next.mutable_wait()->add_ids(initTaskID);
        next.mutable_wait()->mutable_context()->set_intstate(raw(State::Initialization));
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
      next.mutable_wait()->mutable_context()->set_intstate(raw(State::Artifact));
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

result<uint64_t, Error> TaskInterface::requestAction() {
  return static_cast<EngineImpl*>(impl)->taskRequestAction(ctx);
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
               std::unique_ptr<RuleProvider>&& init)
    : impl(new EngineImpl(config, casDB, cache)) {
  static_cast<EngineImpl*>(impl)->internalRegisterRuleProvider(std::move(init));
}

Engine::~Engine() { delete static_cast<EngineImpl*>(impl); }

const EngineConfig& Engine::config() {
  return static_cast<EngineImpl*>(impl)->config();
}

Build Engine::build(const Label& artifact) {
  return static_cast<EngineImpl*>(impl)->build(artifact);
}

#pragma mark - LabelTrie

template <typename entry_type>
bool LabelTrie<entry_type>::insert(std::pair<const Label&, entry_type> entry) {
  auto& [label, value] = entry;
  node_map* current = &root;
  int idx = 0;
  while (idx < label.components_size() - 1) {
    auto component = label.components(idx);
    if (current->contains(component)) {
      auto& next = current->at(component);
      if (std::holds_alternative<node_map>(next.contents)) {
        current = &std::get<node_map>(next.contents);
        idx++;
        continue;
      } else {
        return false;
      }
    }

    current->insert({component, node{node_map()}});
    current = &std::get<node_map>(current->at(component).contents);
    idx++;
  }

  auto lastComponent = label.components(idx);
  if (prefixOnly) {
    current->insert({lastComponent, {value}});
    return true;
  }

  current->insert({lastComponent, node{node_map()}});

  std::string target;
  if (label.name().empty()) {
    target = ":" + lastComponent;
  } else {
    target = ":" + label.name();
  }
  current = &std::get<node_map>(current->at(lastComponent).contents);
  current->insert({target, {value}});

  return true;
}

template <typename entry_type>
std::optional<entry_type>
LabelTrie<entry_type>::operator[](const Label& label) const {
  if (label.components_size() == 0)
    return {};

  const node_map* current = &root;
  for (auto component : label.components()) {
    if (!current->contains(component)) {
      return {};
    }

    auto& next = current->at(component);
    if (std::holds_alternative<entry_type>(next.contents)) {
      return std::get<entry_type>(next.contents);
    }
    current = &std::get<node_map>(next.contents);
  }

  if (prefixOnly)
    return {};

  std::string target;
  if (label.name().empty()) {
    target = ":" + label.components(label.components_size() - 1);
  } else {
    target = ":" + label.name();
  }
  if (current->contains(target)) {
    auto& next = current->at(target);
    if (std::holds_alternative<entry_type>(next.contents)) {
      return std::get<entry_type>(next.contents);
    }
  }

  return {};
}
