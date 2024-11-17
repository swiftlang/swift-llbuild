//===- SwiftAdaptors.cpp ----------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include <llbuild3/Errors.hpp>
#include <llbuild3/SwiftAdaptors.hpp>

#include "llbuild3/core/ActionCache.h"
#include "llbuild3/core/CAS.h"
#include "llbuild3/core/Engine.h"

#include <cassert>

using namespace llbuild3;
using namespace llbuild3::core;

namespace {
inline Error makeProtoError() {
  Error err;
  err.set_type(ErrorType::ENGINE);
  err.set_code(EngineError::InternalProtobufSerialization);
  return err;
}
} // namespace

namespace llbuild3 {
namespace core {

class ExtTaskAdaptor : public Task {
private:
  ExtTask task;
  Label taskName;
  Signature taskSignature;

public:
  ExtTaskAdaptor(ExtTask task) : Task(Task::Properties(task.isInit, true)), task(task) {
    auto success = taskName.ParseFromString(task.name);
    assert(success);

    success = taskSignature.ParseFromString(task.signature);
    assert(success);

    (void)success;
  }

  const Label& name() const { return taskName; }
  const Signature& signature() const { return taskSignature; }

  std::vector<Label> produces() const {
    std::vector<LabelPB> rawProducts;
    task.producesFn(task.ctx, &rawProducts);
    std::vector<Label> products;
    Label current;
    for (auto product : rawProducts) {
      if (current.ParseFromString(product)) {
        products.push_back(current);
      }
    }

    return products;
  }

  TaskNextState compute(TaskInterface ti, const TaskContext& ctx,
                        const TaskInputs& inputs) {
    ExtTaskInterface eti(ti.impl, ti.ctx);
    TaskContextPB tc;
    if (!ctx.SerializeToString(&tc)) {
      TaskNextState res;
      *res.mutable_error() = makeProtoError();
      return res;
    }

    TaskInputsPB in;
    if (!inputs.SerializeToString(&in)) {
      TaskNextState res;
      *res.mutable_error() = makeProtoError();
      return res;
    }

    std::string ns;
    if (!task.computeFn(task.ctx, eti, &tc, &in, &ns)) {
      TaskNextState res;
      *res.mutable_error() = makeProtoError();
      return res;
    }

    TaskNextState res;
    if (!res.ParseFromString(ns)) {
      *res.mutable_error() = makeProtoError();
    }

    return res;
  }
};

class ExtRuleAdaptor : public Rule {
private:
  ExtRule rule;
  Label ruleName;
  Signature ruleSignature;

public:
  ExtRuleAdaptor(ExtRule rule) : rule(rule) {
    auto success = ruleName.ParseFromString(rule.name);
    assert(success);

    success = ruleSignature.ParseFromString(rule.signature);
    assert(success);
  }

  const Label& name() const { return ruleName; }
  const Signature& signature() const { return ruleSignature; }

  std::vector<Label> produces() const {
    std::vector<LabelPB> rawProducts;
    rule.producesFn(rule.ctx, &rawProducts);
    std::vector<Label> products;
    Label current;
    for (auto product : rawProducts) {
      if (current.ParseFromString(product)) {
        products.push_back(current);
      }
    }

    return products;
  }

  std::unique_ptr<Task> configureTask() {
    ExtTask ext;
    if (rule.configureTaskFn(rule.ctx, &ext)) {
      return std::unique_ptr<Task>(new ExtTaskAdaptor(ext));
    }
    return {nullptr};
  }
};

class ExtRuleProviderAdaptor : public RuleProvider {
private:
  ExtRuleProvider ruleProvider;

public:
  ExtRuleProviderAdaptor(ExtRuleProvider ruleProvider)
      : ruleProvider(ruleProvider) {}

  std::vector<Label> rulePrefixes() {
    std::vector<LabelPB> prefixes;
    ruleProvider.rulePrefixesFn(ruleProvider.ctx, &prefixes);
    return prefixesFrom(prefixes);
  }

  std::vector<Label> artifactPrefixes() {
    std::vector<LabelPB> prefixes;
    ruleProvider.artifactPrefixesFn(ruleProvider.ctx, &prefixes);
    return prefixesFrom(prefixes);
  }

  std::unique_ptr<Rule> ruleByName(const Label& name) {
    LabelPB lbl;
    if (!name.SerializeToString(&lbl)) {
      return {nullptr};
    }

    ExtRule erule;
    if (ruleProvider.ruleByNameFn(ruleProvider.ctx, &lbl, &erule)) {
      return std::unique_ptr<Rule>(new ExtRuleAdaptor(erule));
    }

    return {nullptr};
  }

  std::unique_ptr<Rule> ruleForArtifact(const Label& artifact) {
    LabelPB lbl;
    if (!artifact.SerializeToString(&lbl)) {
      return {nullptr};
    }

    ExtRule erule;
    if (ruleProvider.ruleForArtifactFn(ruleProvider.ctx, &lbl, &erule)) {
      return std::unique_ptr<Rule>(new ExtRuleAdaptor(erule));
    }

    return {nullptr};
  }

private:
  std::vector<Label> prefixesFrom(const std::vector<LabelPB>& rawPrefixes) {
    std::vector<Label> prefixes;
    Label current;
    for (auto prefix : rawPrefixes) {
      if (current.ParseFromString(prefix)) {
        prefixes.push_back(current);
      }
    }

    return prefixes;
  }
};

ErrorPB ExtTaskInterface::registerRuleProvider(const ExtRuleProvider provider) {
  std::unique_ptr<RuleProvider> rp(new ExtRuleProviderAdaptor(provider));

  TaskInterface ti(impl, ctx);

  auto res = ti.registerRuleProvider(std::move(rp));
  if (res.has_value()) {
    ErrorPB err;
    res->SerializeToString(&err);
    return err;
  }

  return {};
}

result<uint64_t, ErrorPB>
ExtTaskInterface::requestArtifact(const LabelPB label) {
  Label lbl;
  lbl.ParseFromString(label);

  TaskInterface ti(impl, ctx);
  auto res = ti.requestArtifact(lbl);
  if (res.has_error()) {
    ErrorPB err;
    res.error().SerializeToString(&err);
    return fail(err);
  }

  return *res;
}

result<uint64_t, ErrorPB> ExtTaskInterface::requestRule(const LabelPB label) {
  Label lbl;
  lbl.ParseFromString(label);

  TaskInterface ti(impl, ctx);
  auto res = ti.requestRule(lbl);
  if (res.has_error()) {
    ErrorPB err;
    res.error().SerializeToString(&err);
    return fail(err);
  }

  return *res;
}

result<uint64_t, ErrorPB> ExtTaskInterface::requestAction() {
  //    Label lbl;
  //    lbl.ParseFromString(label);

  TaskInterface ti(impl, ctx);
  auto res = ti.requestAction();
  if (res.has_error()) {
    ErrorPB err;
    res.error().SerializeToString(&err);
    return fail(err);
  }

  return *res;
}


CASDatabaseRef makeInMemoryCASDatabase() {
  return CASDatabaseRef(new InMemoryCASDatabase());
}


class ExtActionCacheAdaptor: public ActionCache {
private:
  ExtActionCache extCache;

public:
  ExtActionCacheAdaptor(ExtActionCache extCache) : extCache(extCache) { }

  void get(const CacheKey& key, std::function<void(result<CacheValue, Error>)> resultHandler) {
    CacheKeyPB kpb;
    if (!key.SerializeToString(&kpb)) {
      Error err;
      err.set_type(ErrorType::ENGINE);
      err.set_code(InternalProtobufSerialization);
      resultHandler(fail(err));
    }

    extCache.getFn(extCache.ctx, kpb, std::function([resultHandler](CacheValuePB value, ErrorPB error) {
      if (error.size() > 0) {
        Error err;
        err.ParseFromString(error);
        resultHandler(fail(err));
      } else {
        CacheValue val;
        val.ParseFromString(value);
        resultHandler(val);
      }
    }));
  }

  void update(const CacheKey& key, const CacheValue& value) {
    CacheKeyPB kpb;
    if (!key.SerializeToString(&kpb)) {
      return;
    }
    CacheValuePB vpb;
    if (!value.SerializeToString(&vpb)) {
      return;
    }

    extCache.updateFn(extCache.ctx, kpb, vpb);
  }
};

ActionCacheRef makeExtActionCache(ExtActionCache extCache) {
  return ActionCacheRef(new ExtActionCacheAdaptor(extCache));
}

ActionCacheRef makeInMemoryActionCache() {
  return ActionCacheRef(new InMemoryActionCache());
}


void BuildRef::cancel() { build->cancel(); }

void BuildRef::addCompletionHandler(
    void* ctx, void (*handler)(void*, result<ArtifactPB, ErrorPB>*)) {
  build->addCompletionHandler([ctx, handler](result<Artifact, Error> res) {
    if (res.has_error()) {
      ErrorPB err;
      res.error().SerializeToString(&err);
      result<ArtifactPB, ErrorPB> r = fail(err);
      handler(ctx, &r);
      return;
    }

    ArtifactPB art;
    if (!res->SerializeToString(&art)) {
      ErrorPB err;
      makeProtoError().SerializeToString(&err);
      result<ArtifactPB, ErrorPB> r = fail(err);
      handler(ctx, &r);
      return;
    }
    result<ArtifactPB, ErrorPB> r = art;
    handler(ctx, &r);
  });
}

BuildRef EngineRef::build(const LabelPB artifact) {
  Label art;
  art.ParseFromString(artifact);

  auto build = engine->build(art);
  return BuildRef(std::shared_ptr<Build>(new Build(build)));
}

EngineRef makeEngine(ExtEngineConfig config, CASDatabaseRef casDB,
                     ActionCacheRef cache, const ExtRuleProvider provider) {
  std::unique_ptr<RuleProvider> rp(new ExtRuleProviderAdaptor(provider));

  EngineConfig intcfg;
  if (config.initRule.has_value()) {
    Label lbl;
    lbl.ParseFromString(*config.initRule);
    intcfg.initRule = lbl;
  }

  return EngineRef(std::shared_ptr<Engine>(new Engine(intcfg, casDB, cache, std::move(rp))));
}

} // namespace core
} // namespace llbuild3
