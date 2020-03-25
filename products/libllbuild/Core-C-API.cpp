//===-- Core-C-API.cpp ----------------------------------------------------===//
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

// Include the public API.
#include <llbuild/llbuild.h>

#include "llbuild/Basic/ExecutionQueue.h"
#include "llbuild/Basic/Tracing.h"
#include "llbuild/Core/BuildDB.h"
#include "llbuild/Core/BuildEngine.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstring>
#include <thread>

using namespace llbuild;
using namespace llbuild::core;
using namespace llbuild::basic;

/* Build Engine API */

namespace {

class CAPIBuildEngineDelegate : public BuildEngineDelegate, public basic::ExecutionQueueDelegate {
  llb_buildengine_delegate_t cAPIDelegate;
  llb_buildengine_invocation_t invocation;

  friend class CAPITask;

  struct CAPIRule : public Rule {
    llb_rule_t rule;
    void* engineContext = nullptr;

    CAPIRule(const KeyType& key) : Rule(key) { }

    Task* createTask(BuildEngine& engine) override {
      return (Task*) rule.create_task(rule.context, engineContext);
    }

    bool isResultValid(BuildEngine&, const ValueType& value) override {
      if (!rule.is_result_valid) return true;

      llb_data_t value_data{ value.size(), value.data() };
      return rule.is_result_valid(rule.context, engineContext, &rule,
                                  &value_data);
    }

    void updateStatus(BuildEngine&, Rule::StatusKind status) override {
      if (!rule.update_status) return;

      rule.update_status(rule.context, engineContext,
                         (llb_rule_status_kind_t)status);
    }
  };

  virtual ~CAPIBuildEngineDelegate() {
    if (cAPIDelegate.destroy_context) {
      cAPIDelegate.destroy_context(cAPIDelegate.context);
    }
  }

  virtual std::unique_ptr<Rule> lookupRule(const KeyType& key) override {
    CAPIRule* capiRule = new CAPIRule(key);
    capiRule->engineContext = cAPIDelegate.context;
    llb_data_t key_data{ key.size(), (const uint8_t*)key.data() };
    cAPIDelegate.lookup_rule(cAPIDelegate.context, &key_data, &capiRule->rule);

    // FIXME: Check that the client created the rule appropriately. We should
    // change the API to be type safe here, by forcing the client to return a
    // handle created by the C API.
    assert(capiRule->rule.create_task && "client failed to initialize rule");

    return std::unique_ptr<Rule>(capiRule);
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    // FIXME.
    assert(0 && "unexpected cycle!");
  }

  virtual void error(const Twine& message) override {
    cAPIDelegate.error(cAPIDelegate.context, message.str().c_str());
  }

  void processStarted(basic::ProcessContext*, basic::ProcessHandle) override { }
  void processHadError(basic::ProcessContext*, basic::ProcessHandle, const Twine&) override { }
  void processHadOutput(basic::ProcessContext*, basic::ProcessHandle, StringRef) override { }
  void processFinished(basic::ProcessContext*, basic::ProcessHandle, const basic::ProcessResult&) override { }
  void queueJobStarted(basic::JobDescriptor*) override { }
  void queueJobFinished(basic::JobDescriptor*) override { }

  std::unique_ptr<basic::ExecutionQueue> createExecutionQueue() override {
    if (invocation.useSerialBuild) {
      return createSerialQueue(*this, nullptr);
    }

    int numLanes = invocation.schedulerLanes;

    if (numLanes == 0) {
      unsigned numCPUs = std::thread::hardware_concurrency();
      if (numCPUs == 0) {
        numLanes = 1;
      } else {
        numLanes = numCPUs;
      }
    }

    SchedulerAlgorithm schedulerAlgorithm;

    switch(invocation.schedulerAlgorithm) {
    case llb_scheduler_algorithm_fifo:
      schedulerAlgorithm = SchedulerAlgorithm::FIFO;
      break;
    case llb_scheduler_algorithm_command_name_priority:
      schedulerAlgorithm = SchedulerAlgorithm::NamePriority;
      break;
    }

    return std::unique_ptr<basic::ExecutionQueue>(
            createLaneBasedExecutionQueue(*this, numLanes,
                                          schedulerAlgorithm,
                                          nullptr));
  }

public:
  CAPIBuildEngineDelegate(llb_buildengine_delegate_t delegate, llb_buildengine_invocation_t invocation)
    : cAPIDelegate(delegate), invocation(invocation)
  {
    
  }
};

/// Holds onto the pointers for both the build engine and delegate objects so
/// that both of them can be properly cleaned up when destroy() is called.
struct CAPIBuildEngine {
  std::unique_ptr<BuildEngineDelegate> delegate;
  std::unique_ptr<BuildEngine> engine;
};

class CAPITask : public Task {
  llb_task_delegate_t cAPIDelegate;

public:
  CAPITask(llb_task_delegate_t delegate) : cAPIDelegate(delegate) {
      assert(cAPIDelegate.start && "missing task start function");
      assert(cAPIDelegate.provide_value &&
             "missing task provide_value function");
      assert(cAPIDelegate.inputs_available &&
             "missing task inputs_available function");
  }

  virtual ~CAPITask() {
    if (cAPIDelegate.destroy_context) {
      cAPIDelegate.destroy_context(cAPIDelegate.context);
    }
  }

  virtual void start(TaskInterface& ti) override {
    CAPIBuildEngineDelegate* delegate =
      static_cast<CAPIBuildEngineDelegate*>(ti.delegate());
    cAPIDelegate.start(cAPIDelegate.context,
                       delegate->cAPIDelegate.context,
                       (llb_task_interface_t*)(&ti));
  }

  virtual void provideValue(TaskInterface& ti, uintptr_t inputID,
                            const ValueType& value) override {
    CAPIBuildEngineDelegate* delegate =
      static_cast<CAPIBuildEngineDelegate*>(ti.delegate());
    llb_data_t valueData{ value.size(), value.data() };
    cAPIDelegate.provide_value(cAPIDelegate.context,
                               delegate->cAPIDelegate.context,
                               (llb_task_interface_t*)(&ti),
                               inputID, &valueData);
  }

  virtual void inputsAvailable(TaskInterface& ti) override {
    CAPIBuildEngineDelegate* delegate =
      static_cast<CAPIBuildEngineDelegate*>(ti.delegate());
    cAPIDelegate.inputs_available(cAPIDelegate.context,
                                  delegate->cAPIDelegate.context,
                                  (llb_task_interface_t*)(&ti));
  }
};

};

llb_buildengine_t* llb_buildengine_create(llb_buildengine_delegate_t delegate, llb_buildengine_invocation_t invocation) {
  CAPIBuildEngine* capi_engine = new CAPIBuildEngine;
  capi_engine->delegate = std::unique_ptr<BuildEngineDelegate>(
    new CAPIBuildEngineDelegate(delegate, invocation));
  capi_engine->engine = std::unique_ptr<BuildEngine>(
    new BuildEngine(*capi_engine->delegate));
  return (llb_buildengine_t*) capi_engine;
}

void llb_buildengine_destroy(llb_buildengine_t* engine) {
  // FIXME: Delegate is lost.
  delete (CAPIBuildEngine*)engine;
}

void llb_data_destroy(llb_data_t *data) {
  free((char *)data->data);
}

bool llb_buildengine_attach_db(llb_buildengine_t* engine_p,
                               const llb_data_t* path,
                               uint32_t schema_version,
                               char** error_out) {
  BuildEngine& engine = *((CAPIBuildEngine*) engine_p)->engine;

  std::string error;
  std::unique_ptr<BuildDB> db(createSQLiteBuildDB(
                                  std::string((char*)path->data,
                                              path->length),
                                  schema_version,
                                  /* recreateUnmatchedVersion = */ true,
                                  &error));
  if (!db) {
    *error_out = strdup(error.c_str());
    return false;
  }

  bool result = engine.attachDB(std::move(db), &error);
  *error_out = strdup(error.c_str());
  return result;
}

void llb_buildengine_build(llb_buildengine_t* engine_p, const llb_data_t* key,
                           llb_data_t* result_out) {
  auto& engine = ((CAPIBuildEngine*) engine_p)->engine;

  auto& result = engine->build(KeyType((const char*)key->data, key->length));

  *result_out = llb_data_t{ result.size(), result.data() };
}

void llb_buildengine_task_needs_input(llb_task_interface_t* ti_p,
                                      const llb_data_t* key,
                                      uintptr_t input_id) {
  auto ti = ((TaskInterface*) ti_p);
  ti->request(KeyType((const char*)key->data, key->length), input_id);
}

void llb_buildengine_task_must_follow(llb_task_interface_t* ti_p,
                                      const llb_data_t* key) {
  auto ti = ((TaskInterface*) ti_p);
  ti->mustFollow(KeyType((const char*)key->data, key->length));
}

void llb_buildengine_task_discovered_dependency(llb_task_interface_t* ti_p,
                                                const llb_data_t* key) {
  auto ti = ((TaskInterface*) ti_p);
  ti->discoveredDependency(KeyType((const char*)key->data, key->length));
}

void llb_buildengine_task_is_complete(llb_task_interface_t* ti_p,
                                      const llb_data_t* value,
                                      bool force_change) {
  auto ti = ((TaskInterface*) ti_p);
  std::vector<uint8_t> result(value->length);
  memcpy(result.data(), value->data, value->length);
  ti->complete(std::move(result));
}

// FIXME: We need to expose customizing the JobDescriptor somehow. Can't be
// nullptr since those are used as sentinels that the execution queues are
// finishing.
class CAPIJobDescriptor : public JobDescriptor {
public:
  CAPIJobDescriptor() {}

  virtual StringRef getOrdinalName() const {
      return StringRef("");
  }

  virtual void getShortDescription(SmallVectorImpl<char> &result) const {
      llvm::raw_svector_ostream(result) << getOrdinalName();
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) const {
      llvm::raw_svector_ostream(result) << getOrdinalName();
  }
};

void llb_buildengine_spawn(llb_task_interface_t* ti_p, void* context, void (*action)(void* context, llb_task_interface_t* ti_p)) {
    auto ti = ((TaskInterface*) ti_p);
    auto ti_cp = TaskInterface(*ti);
    auto fn = [context, ti_cp, action](QueueJobContext* jobContext) mutable {
        action(context, (llb_task_interface_t*)&ti_cp);
    };
    auto jd = new CAPIJobDescriptor();
    ti->spawn({ std::move(jd), std::move(fn) });
}

llb_task_t* llb_task_create(llb_task_delegate_t delegate) {
  return (llb_task_t*) new CAPITask(delegate);
}

void llb_enable_tracing() {
  TracingEnabled = true;
}

void llb_disable_tracing() {
  TracingEnabled = false;
}
