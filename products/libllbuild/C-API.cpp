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
  static std::string VersionString = getLLBuildFullVersion();

  return VersionString.c_str();
}

/* Build Engine API */

namespace {

class CAPIBuildEngineDelegate : public BuildEngineDelegate {
  llb_buildengine_delegate_t CAPIDelegate;

  virtual ~CAPIBuildEngineDelegate() {
    if (CAPIDelegate.destroy_context) {
      CAPIDelegate.destroy_context(CAPIDelegate.context);
    }
  }

  virtual Rule lookupRule(const KeyType& Key) override {
    llb_rule_t rule;
    llb_data_t key_data{ Key.length(), (const uint8_t*)Key.data() };
    CAPIDelegate.lookup_rule(CAPIDelegate.context, &key_data, &rule);

    std::function<bool(const Rule&, const ValueType&)> IsResultValid;
    if (rule.is_result_valid) {
      IsResultValid = [rule] (const Rule& NativeRule, const ValueType& Value) {
        // FIXME: Why do we pass the rule here, it is redundant. NativeRule
        // should be == rule here.
        llb_data_t value_data{ Value.size(), Value.data() };
        return rule.is_result_valid(rule.context, &rule, &value_data);
      };
    }

    return Rule{
      KeyType((const char*)rule.key.data, rule.key.length),
      [rule] (BuildEngine& engine) {
        return (Task*) rule.create_task(
          rule.context, (llb_buildengine_t*) &engine);
      },
      IsResultValid };
  }

  virtual void cycleDetected(const std::vector<core::Rule*>& Items) override {
    // FIXME.
    assert(0 && "unexpected cycle!");
  }

public:
  CAPIBuildEngineDelegate(llb_buildengine_delegate_t delegate)
    : CAPIDelegate(delegate)
  {
    
  }
};

class CAPITask : public Task {
  llb_task_delegate_t CAPIDelegate;

public:
  CAPITask(llb_task_delegate_t delegate)
    : Task("C-API-Task"), CAPIDelegate(delegate)
  {
    
  }

  virtual void start(BuildEngine& Engine) override {
    CAPIDelegate.start(CAPIDelegate.context, (llb_task_t*)this,
                       (llb_buildengine_t*) &Engine);
  }

  virtual void provideValue(BuildEngine& Engine, uintptr_t InputID,
                            const ValueType& Value) override {
    llb_data_t ValueData{ Value.size(), Value.data() };
    CAPIDelegate.provide_value(CAPIDelegate.context, (llb_task_t*)this,
                               (llb_buildengine_t*) &Engine,
                               InputID, &ValueData);
  }

  virtual void inputsAvailable(BuildEngine& Engine) override {
    CAPIDelegate.inputs_available(CAPIDelegate.context, (llb_task_t*)this,
                                  (llb_buildengine_t*) &Engine);
  }
};

};

llb_buildengine_t* llb_buildengine_create(llb_buildengine_delegate_t delegate) {
  // FIXME: Delegate is leaked, need to provide a provision for owning the
  // delegate.
  BuildEngineDelegate* Delegate = new CAPIBuildEngineDelegate(delegate);
  return (llb_buildengine_t*) new BuildEngine(*Delegate);
}

void llb_buildengine_destroy(llb_buildengine_t* engine) {
  // FIXME: Delegate is lost.
  delete (BuildEngine*)engine;
}

void llb_buildengine_build(llb_buildengine_t* engine_p, const llb_data_t* key,
                           llb_data_t* result_out) {
  BuildEngine* Engine = (BuildEngine*) engine_p;

  auto Result = Engine->build(KeyType((const char*)key->data, key->length));

  *result_out = llb_data_t{ Result.size(), Result.data() };
}

llb_task_t* llb_buildengine_register_task(llb_buildengine_t* engine_p,
                                          llb_task_t* task) {
  BuildEngine* Engine = (BuildEngine*) engine_p;
  Engine->registerTask((Task*)task);
  return task;
}

void llb_buildengine_task_needs_input(llb_buildengine_t* engine_p,
                                      llb_task_t* task,
                                      const llb_data_t* key,
                                      uintptr_t input_id) {
  BuildEngine* Engine = (BuildEngine*) engine_p;
  Engine->taskNeedsInput((Task*)task,
                         KeyType((const char*)key->data, key->length),
                         input_id);
}

void llb_buildengine_task_must_follow(llb_buildengine_t* engine_p,
                                      llb_task_t* task,
                                      const llb_data_t* key) {
  BuildEngine* Engine = (BuildEngine*) engine_p;
  Engine->taskMustFollow((Task*)task,
                         KeyType((const char*)key->data, key->length));
}

void llb_buildengine_task_discovered_dependency(llb_buildengine_t* engine_p,
                                                llb_task_t* task,
                                                const llb_data_t* key) {
  BuildEngine* Engine = (BuildEngine*) engine_p;
  Engine->taskDiscoveredDependency((Task*)task,
                                   KeyType((const char*)key->data,
                                           key->length));
}

void llb_buildengine_task_is_complete(llb_buildengine_t* engine_p,
                                      llb_task_t* task,
                                      const llb_data_t* value) {
  BuildEngine* Engine = (BuildEngine*) engine_p;
  std::vector<uint8_t> Result(value->length);
  memcpy(Result.data(), value->data, value->length);
  Engine->taskIsComplete((Task*)task, std::move(Result));
}

llb_task_t* llb_task_create(llb_task_delegate_t delegate) {
  return (llb_task_t*) new CAPITask(delegate);
}
