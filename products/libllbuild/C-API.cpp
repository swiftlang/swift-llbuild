//===-- C-API.cpp ---------------------------------------------------------===//
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

#include "llbuild/Basic/Version.h"
#include "llbuild/Core/BuildEngine.h"

using namespace llbuild;
using namespace llbuild::core;

/* Misc API */

const char* llb_get_full_version_string(void) {
  // Use a static local to store the version string, to avoid lifetime issues.
  static std::string versionString = getLLBuildFullVersion();

  return versionString.c_str();
}

/* Build Engine API */

namespace {

class CAPIBuildEngineDelegate : public BuildEngineDelegate {
  llb_buildengine_delegate_t cAPIDelegate;

  virtual ~CAPIBuildEngineDelegate() {
    if (cAPIDelegate.destroy_context) {
      cAPIDelegate.destroy_context(cAPIDelegate.context);
    }
  }

  virtual Rule lookupRule(const KeyType& key) override {
    llb_rule_t rule;
    llb_data_t key_data{ key.length(), (const uint8_t*)key.data() };
    cAPIDelegate.lookup_rule(cAPIDelegate.context, &key_data, &rule);

    std::function<bool(const Rule&, const ValueType&)> isResultValid;
    if (rule.is_result_valid) {
      isResultValid = [rule] (const Rule& nativeRule, const ValueType& value) {
        // FIXME: Why do we pass the rule here, it is redundant. NativeRule
        // should be == rule here.
        llb_data_t value_data{ value.size(), value.data() };
        return rule.is_result_valid(rule.context, &rule, &value_data);
      };
    }

    return Rule{
      KeyType((const char*)rule.key.data, rule.key.length),
      [rule] (BuildEngine& engine) {
        return (Task*) rule.create_task(
          rule.context, (llb_buildengine_t*) &engine);
      },
      isResultValid };
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& items) override {
    // FIXME.
    assert(0 && "unexpected cycle!");
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
  CAPITask(llb_task_delegate_t delegate)
    : Task("C-API-Task"), cAPIDelegate(delegate)
  {
    
  }

  virtual void start(BuildEngine& engine) override {
    cAPIDelegate.start(cAPIDelegate.context, (llb_task_t*)this,
                       (llb_buildengine_t*) &engine);
  }

  virtual void provideValue(BuildEngine& engine, uintptr_t inputID,
                            const ValueType& value) override {
    llb_data_t valueData{ value.size(), value.data() };
    cAPIDelegate.provide_value(cAPIDelegate.context, (llb_task_t*)this,
                               (llb_buildengine_t*) &engine,
                               inputID, &valueData);
  }

  virtual void inputsAvailable(BuildEngine& engine) override {
    cAPIDelegate.inputs_available(cAPIDelegate.context, (llb_task_t*)this,
                                  (llb_buildengine_t*) &engine);
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

void llb_buildengine_build(llb_buildengine_t* engine_p, const llb_data_t* key,
                           llb_data_t* result_out) {
  BuildEngine* engine = (BuildEngine*) engine_p;

  auto result = engine->build(KeyType((const char*)key->data, key->length));

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
                                      const llb_data_t* value) {
  BuildEngine* engine = (BuildEngine*) engine_p;
  std::vector<uint8_t> result(value->length);
  memcpy(result.data(), value->data, value->length);
  engine->taskIsComplete((Task*)task, std::move(result));
}

llb_task_t* llb_task_create(llb_task_delegate_t delegate) {
  return (llb_task_t*) new CAPITask(delegate);
}
