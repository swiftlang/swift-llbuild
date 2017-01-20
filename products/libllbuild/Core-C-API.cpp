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

#include "llbuild/Core/BuildDB.h"
#include "llbuild/Core/BuildEngine.h"

#include <cassert>
#include <cstring>

using namespace llbuild;
using namespace llbuild::core;

/* Build Engine API */

namespace {

class CAPIBuildEngineDelegate : public BuildEngineDelegate {
  llb_buildengine_delegate_t cAPIDelegate;

  friend class CAPITask;

  virtual ~CAPIBuildEngineDelegate() {
    if (cAPIDelegate.destroy_context) {
      cAPIDelegate.destroy_context(cAPIDelegate.context);
    }
  }

  virtual Rule lookupRule(const KeyType& key) override {
    void* engineContext = cAPIDelegate.context;
    llb_rule_t rule{};
    llb_data_t key_data{ key.length(), (const uint8_t*)key.data() };
    cAPIDelegate.lookup_rule(cAPIDelegate.context, &key_data, &rule);

    // FIXME: Check that the client created the rule appropriately. We should
    // change the API to be type safe here, by forcing the client to return a
    // handle created by the C API.
    assert(rule.create_task && "client failed to initialize rule");

    std::function<bool(BuildEngine&, const Rule&,
                       const ValueType&)> isResultValid;
    if (rule.is_result_valid) {
      isResultValid = [rule, engineContext] (BuildEngine&,
                                             const Rule& nativeRule,
                                             const ValueType& value) {
        // FIXME: Why do we pass the rule here, it is redundant. NativeRule
        // should be == rule here.
        llb_data_t value_data{ value.size(), value.data() };
        return rule.is_result_valid(rule.context, engineContext, &rule,
                                    &value_data);
      };
    }

    std::function<void(BuildEngine&, Rule::StatusKind)> updateStatus;
    if (rule.update_status) {
      updateStatus = [rule, engineContext] (BuildEngine&,
                                            Rule::StatusKind kind) {
        return rule.update_status(rule.context, engineContext,
                                  (llb_rule_status_kind_t)kind);
      };
    }

    return Rule{
      // FIXME: This is a wasteful copy.
      key,
      [rule, engineContext] (BuildEngine& engine) {
        return (Task*) rule.create_task(rule.context, engineContext);
      },
      isResultValid,
      updateStatus };
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    // FIXME.
    assert(0 && "unexpected cycle!");
  }

  virtual void error(const Twine& message) override {
    cAPIDelegate.error(cAPIDelegate.context, message.str().c_str());
  }

public:
  CAPIBuildEngineDelegate(llb_buildengine_delegate_t delegate)
    : cAPIDelegate(delegate)
  {
    
  }
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

  virtual void start(BuildEngine& engine) override {
    CAPIBuildEngineDelegate* delegate =
      static_cast<CAPIBuildEngineDelegate*>(engine.getDelegate());
    cAPIDelegate.start(cAPIDelegate.context,
                       delegate->cAPIDelegate.context,
                       (llb_task_t*)this);
  }

  virtual void provideValue(BuildEngine& engine, uintptr_t inputID,
                            const ValueType& value) override {
    CAPIBuildEngineDelegate* delegate =
      static_cast<CAPIBuildEngineDelegate*>(engine.getDelegate());
    llb_data_t valueData{ value.size(), value.data() };
    cAPIDelegate.provide_value(cAPIDelegate.context,
                               delegate->cAPIDelegate.context,
                               (llb_task_t*)this,
                               inputID, &valueData);
  }

  virtual void inputsAvailable(BuildEngine& engine) override {
    CAPIBuildEngineDelegate* delegate =
      static_cast<CAPIBuildEngineDelegate*>(engine.getDelegate());
    cAPIDelegate.inputs_available(cAPIDelegate.context,
                                  delegate->cAPIDelegate.context,
                                  (llb_task_t*)this);
  }
};

};

llb_buildengine_t* llb_buildengine_create(llb_buildengine_delegate_t delegate) {
  // FIXME: Delegate is leaked, need to provide a provision for owning the
  // delegate.
  BuildEngineDelegate* engine_delegate = new CAPIBuildEngineDelegate(delegate);
  return (llb_buildengine_t*) new BuildEngine(*engine_delegate);
}

void llb_buildengine_destroy(llb_buildengine_t* engine) {
  // FIXME: Delegate is lost.
  delete (BuildEngine*)engine;
}

bool llb_buildengine_attach_db(llb_buildengine_t* engine_p,
                               const llb_data_t* path,
                               uint32_t schema_version,
                               char** error_out) {
  BuildEngine* engine = (BuildEngine*) engine_p;

  std::string error;
  std::unique_ptr<BuildDB> db(createSQLiteBuildDB(
                                  std::string((char*)path->data,
                                              path->length),
                                  schema_version,
                                  &error));
  if (!db) {
    *error_out = strdup(error.c_str());
    return false;
  }

  bool result = engine->attachDB(std::move(db), &error);
  *error_out = strdup(error.c_str());
  return result;
}

void llb_buildengine_build(llb_buildengine_t* engine_p, const llb_data_t* key,
                           llb_data_t* result_out) {
  BuildEngine* engine = (BuildEngine*) engine_p;

  auto& result = engine->build(KeyType((const char*)key->data, key->length));

  *result_out = llb_data_t{ result.size(), result.data() };
}

llb_task_t* llb_buildengine_register_task(llb_buildengine_t* engine_p,
                                          llb_task_t* task) {
  BuildEngine* engine = (BuildEngine*) engine_p;
  engine->registerTask((Task*)task);
  return task;
}

void llb_buildengine_task_needs_input(llb_buildengine_t* engine_p,
                                      llb_task_t* task,
                                      const llb_data_t* key,
                                      uintptr_t input_id) {
  BuildEngine* engine = (BuildEngine*) engine_p;
  engine->taskNeedsInput((Task*)task,
                         KeyType((const char*)key->data, key->length),
                         input_id);
}

void llb_buildengine_task_must_follow(llb_buildengine_t* engine_p,
                                      llb_task_t* task,
                                      const llb_data_t* key) {
  BuildEngine* engine = (BuildEngine*) engine_p;
  engine->taskMustFollow((Task*)task,
                         KeyType((const char*)key->data, key->length));
}

void llb_buildengine_task_discovered_dependency(llb_buildengine_t* engine_p,
                                                llb_task_t* task,
                                                const llb_data_t* key) {
  BuildEngine* engine = (BuildEngine*) engine_p;
  engine->taskDiscoveredDependency((Task*)task,
                                   KeyType((const char*)key->data,
                                           key->length));
}

void llb_buildengine_task_is_complete(llb_buildengine_t* engine_p,
                                      llb_task_t* task,
                                      const llb_data_t* value,
                                      bool force_change) {
  BuildEngine* engine = (BuildEngine*) engine_p;
  std::vector<uint8_t> result(value->length);
  memcpy(result.data(), value->data, value->length);
  engine->taskIsComplete((Task*)task, std::move(result));
}

llb_task_t* llb_task_create(llb_task_delegate_t delegate) {
  return (llb_task_t*) new CAPITask(delegate);
}
