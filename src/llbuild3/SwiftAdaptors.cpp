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

#include "llbuild3/ActionCache.h"
#include "llbuild3/ActionExecutor.h"
#include "llbuild3/LocalExecutor.h"
#include "llbuild3/CAS.h"
#include "llbuild3/Engine.h"

#include <cassert>

using namespace llbuild3;

namespace {
inline Error makeProtoError() {
  Error err;
  err.set_type(ErrorType::ENGINE);
  err.set_code(rawCode(EngineError::InternalProtobufSerialization));
  return err;
}
} // namespace

namespace llbuild3 {

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
  ~ExtTaskAdaptor() {
    task.releaseFn(task.ctx);
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
                        const TaskInputs& inputs, const SubtaskResults& sres) {
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

    SubtaskResultMap smap;
    for (auto entry : sres) {
      if (entry.second.has_error()) {
        ErrorPB err;
        entry.second.error().SerializeToString(&err);
        smap.push_back({entry.first, fail(err)});
      } else {
        smap.push_back({entry.first, std::any_cast<void*>(entry.second.value())});
      }
    }

    std::string ns;
    if (!task.computeFn(task.ctx, eti, &tc, &in, &smap, &ns)) {
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
  ~ExtRuleProviderAdaptor() {
    ruleProvider.releaseFn(ruleProvider.ctx);
  }

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

result<uint64_t, ErrorPB> ExtTaskInterface::requestAction(const ActionPB action) {
  Action act;
  act.ParseFromString(action);

  TaskInterface ti(impl, ctx);
  auto res = ti.requestAction(act);
  if (res.has_error()) {
    ErrorPB err;
    res.error().SerializeToString(&err);
    return fail(err);
  }

  return *res;
}

result<uint64_t, ErrorPB> ExtTaskInterface::spawnSubtask(const ExtSubtask subtask) {
  TaskInterface ti(impl, ctx);
  AsyncSubtask ast{[subtask](SubtaskInterface si, SubtaskCallback cb){
    ExtSubtaskInterface esi(si.impl, si.ctx);

    // Swift is not copying this correctly, so we pass by pointer and manage it
    // ourselves for now
    auto cbptr = new SubtaskCallback(std::move(cb));
    subtask.perform(subtask.ctx, esi, {[cbptr](void* value, ErrorPB error) {
      if (error.size() > 0) {
        Error err;
        err.ParseFromString(error);
        (*cbptr)(fail(err));
        delete cbptr;
        return;
      }
      (*cbptr)(value);
      delete cbptr;
    }});
  }};
  auto res = ti.spawnSubtask({ast});
  if (res.has_error()) {
    ErrorPB err;
    res.error().SerializeToString(&err);
    return fail(err);
  }

  return *res;
}

CASDatabaseRef ExtSubtaskInterface::cas() {
  SubtaskInterface si(impl, ctx);
  return si.cas();
}


class ExtCASDatabaseAdaptor: public CASDatabase {
private:
  ExtCASDatabase extCASDB;

public:
  ExtCASDatabaseAdaptor(ExtCASDatabase extCASDB) : extCASDB(extCASDB) { }
  ~ExtCASDatabaseAdaptor() {
    extCASDB.releaseFn(extCASDB.ctx);
  }

  void contains(const CASObjectID& casid, std::function<void(result<bool, Error>)> resultHandler) {
    extCASDB.containsFn(extCASDB.ctx, casid.bytes(), std::function([resultHandler](bool found, ErrorPB error) {
      if (error.size() > 0) {
        Error err;
        err.ParseFromString(error);
        resultHandler(fail(err));
        return;
      }

      resultHandler(found);
    }));
  }

  void get(const CASObjectID& casid, std::function<void(result<CASObject, Error>)> resultHandler) {
    extCASDB.getFn(extCASDB.ctx, casid.bytes(), std::function([resultHandler](CASObjectPB object, ErrorPB error) {
      if (error.size() > 0) {
        Error err;
        err.ParseFromString(error);
        resultHandler(fail(err));
      } else {
        CASObject obj;
        obj.ParseFromString(object);
        resultHandler(obj);
      }
    }));
  }

  void put(const CASObject& object, std::function<void(result<CASObjectID, Error>)> resultHandler) {
    CASObjectPB opb;
    if (!object.SerializeToString(&opb)) {
      Error err;
      err.set_type(ErrorType::ENGINE);
      err.set_code(rawCode(EngineError::InternalProtobufSerialization));
      resultHandler(fail(err));
      return;
    }

    extCASDB.putFn(extCASDB.ctx, opb, std::function([resultHandler](CASIDBytes casid, ErrorPB error) {
      if (error.size() > 0) {
        Error err;
        err.ParseFromString(error);
        resultHandler(fail(err));
      } else {
        CASObjectID objid;
        *objid.mutable_bytes() = casid;
        resultHandler(objid);
      }
    }));
  }

  CASObjectID identify(const CASObject& object) {
    CASObjectPB opb;
    if (!object.SerializeToString(&opb)) {
      // FIXME: propagate error?
      return CASObjectID();
    }

    CASObjectID objid;
    *objid.mutable_bytes() = extCASDB.identifyFn(extCASDB.ctx, opb);
    return objid;
  }

  void* __raw_context() { return extCASDB.ctx; }
};

CASDatabaseRef makeExtCASDatabase(ExtCASDatabase extCASDB) {
  return CASDatabaseRef(new ExtCASDatabaseAdaptor(extCASDB));
}

CASDatabaseRef makeInMemoryCASDatabase() {
  return CASDatabaseRef(new InMemoryCASDatabase());
}

void* getRawCASDatabaseContext(std::shared_ptr<CASDatabase> casDB) {
  return casDB->__raw_context();
}

void adaptedCASDatabaseContains(CASDatabaseRef casDB, CASIDBytes idbytes, void* ctx, void (*handler)(void*, result<bool, ErrorPB>*)) {
  CASObjectID id;
  *id.mutable_bytes() = idbytes;

  casDB->contains(id, std::function([ctx, handler](result<bool, Error> res) {
    if (res.has_error()) {
      ErrorPB err;
      res.error().SerializeToString(&err);
      result<bool, ErrorPB> sres = fail(err);
      handler(ctx, &sres);
    } else {
      result<bool, ErrorPB> sres = *res;
      handler(ctx, &sres);
    }
  }));
}

void adaptedCASDatabaseGet(CASDatabaseRef casDB, CASIDBytes idbytes, void* ctx, void (*handler)(void*, result<CASObjectPB, ErrorPB>*)) {
  CASObjectID id;
  *id.mutable_bytes() = idbytes;

  casDB->get(id, std::function([ctx, handler](result<CASObject, Error> res) {
    if (res.has_error()) {
      ErrorPB err;
      res.error().SerializeToString(&err);
      result<CASObjectPB, ErrorPB> sres = fail(err);
      handler(ctx, &sres);
    } else {
      CASObjectPB opb;
      if (!res->SerializeToString(&opb)) {
        ErrorPB err;
        makeProtoError().SerializeToString(&err);
        result<CASObjectPB, ErrorPB> r = fail(err);
        handler(ctx, &r);
        return;
      }

      result<CASObjectPB, ErrorPB> sres = opb;
      handler(ctx, &sres);
    }
  }));
}

void adaptedCASDatabasePut(CASDatabaseRef casDB, CASObjectPB opb, void* ctx, void (*handler)(void*, result<CASIDBytes, ErrorPB>*)) {
  CASObject obj;
  obj.ParseFromString(opb);

  casDB->put(obj, std::function([ctx, handler](result<CASObjectID, Error> res) {
    if (res.has_error()) {
      ErrorPB err;
      res.error().SerializeToString(&err);
      result<CASObjectPB, ErrorPB> sres = fail(err);
      handler(ctx, &sres);
    } else {
      result<CASIDBytes, ErrorPB> sres = res->bytes();
      handler(ctx, &sres);
    }
  }));
}

CASIDBytes adaptedCASDatabaseIdentify(CASDatabaseRef casDB, CASObjectPB opb) {
  CASObject obj;
  obj.ParseFromString(opb);

  auto id = casDB->identify(obj);
  return id.bytes();
}


class ExtActionCacheAdaptor: public ActionCache {
private:
  ExtActionCache extCache;

public:
  ExtActionCacheAdaptor(ExtActionCache extCache) : extCache(extCache) { }
  ~ExtActionCacheAdaptor() { extCache.releaseFn(extCache.ctx); }

  void get(const CacheKey& key, std::function<void(result<CacheValue, Error>)> resultHandler) {
    CacheKeyPB kpb;
    if (!key.SerializeToString(&kpb)) {
      Error err;
      err.set_type(ErrorType::ENGINE);
      err.set_code(rawCode(EngineError::InternalProtobufSerialization));
      resultHandler(fail(err));
      return;
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


class ExtLocalSandboxAdaptor: public LocalSandbox {
private:
  ExtLocalSandbox ext;

public:
  ExtLocalSandboxAdaptor(ExtLocalSandbox ext) : ext(ext) { }
  ~ExtLocalSandboxAdaptor() {
    ext.releaseFn(ext.ctx);
  }

  std::filesystem::path workingDir() override {
    std::string path;
    ext.dirFn(ext.ctx, &path);
    return path;
  }

  std::optional<Error> prepareInput(std::string path, FileType type,
                                    CASObjectID objID) override {
    std::string error;
    std::string idbytes = objID.bytes();
    // FIXME: passing all the strings here as pointers because something in
    // FIXME: the swift-interop layer corrupts string ownership with this
    // FIXME: particular construct
    ext.prepareInputFn(ext.ctx, &path, type, &idbytes, &error);
    if (error.size() > 0) {
      Error err;
      err.ParseFromString(error);
      return {err};
    }
    return {};
  }

  result<std::vector<FileObject>, Error>
  collectOutputs(std::vector<std::string> paths) override {
    std::vector<std::string> outputPBs;
    std::string error;
    ext.collectOutputsFn(ext.ctx, paths, &outputPBs, &error);

    if (error.size() > 0) {
      Error err;
      err.ParseFromString(error);
      return fail(err);
    }

    std::vector<FileObject> outputs;
    for (auto fopb: outputPBs) {
      FileObject fo;
      fo.ParseFromString(fopb);
      outputs.emplace_back(std::move(fo));
    }
    return outputs;
  }

  void release() override {
    ext.releaseSandboxFn(ext.ctx);
  }
};

class ExtLocalSandboxProviderAdaptor: public LocalSandboxProvider {
private:
  ExtLocalSandboxProvider ext;

public:
  ExtLocalSandboxProviderAdaptor(ExtLocalSandboxProvider ext) : ext(ext) { }
  ~ExtLocalSandboxProviderAdaptor() {
    ext.releaseFn(ext.ctx);
  }

  result<std::shared_ptr<LocalSandbox>, Error>
  create(ProcessHandle hndl) override {
    std::string errstr;
    auto extLS = ext.createFn(ext.ctx, hndl.id, &errstr);
    if (errstr.size() > 0) {
      Error error;
      error.ParseFromString(errstr);
      return fail(error);
    }
    return std::make_shared<ExtLocalSandboxAdaptor>(extLS);
  }
};

LocalSandboxProviderRef makeExtLocalSandboxProvider(ExtLocalSandboxProvider ext) {
  return std::make_shared<ExtLocalSandboxProviderAdaptor>(ext);
}

ActionExecutorRef makeActionExecutor(
  CASDatabaseRef db,
  ActionCacheRef actionCache,
  LocalExecutorRef local,
  RemoteExecutorRef remote
) {
  return ActionExecutorRef(new ActionExecutor(db, actionCache, local, remote));
}

LocalExecutorRef makeLocalExecutor(LocalSandboxProviderRef sandboxProvider) {
  return std::shared_ptr<LocalExecutor>(new LocalExecutor(sandboxProvider));
}

RemoteExecutorRef makeRemoteExecutor() {
  return {nullptr};
}


void BuildRef::cancel() { build->cancel(); }

void BuildRef::addCompletionHandler(
  void* ctx, void (*handler)(void*, result<ArtifactPB, ErrorPB>*)
) {
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

CASDatabaseRef EngineRef::cas() {
  return engine->cas();
}

BuildRef EngineRef::build(const LabelPB artifact) {
  Label art;
  art.ParseFromString(artifact);

  auto build = engine->build(art);
  return BuildRef(std::shared_ptr<Build>(new Build(build)));
}

EngineRef makeEngine(
  ExtEngineConfig config, CASDatabaseRef casDB, ActionCacheRef cache,
  ActionExecutorRef executor, const ExtRuleProvider provider
) {
  std::unique_ptr<RuleProvider> rp(new ExtRuleProviderAdaptor(provider));

  EngineConfig intcfg;
  if (config.initRule.has_value()) {
    Label lbl;
    lbl.ParseFromString(*config.initRule);
    intcfg.initRule = lbl;
  }

  return EngineRef(std::shared_ptr<Engine>(
    new Engine(intcfg, casDB, cache, executor, std::move(rp)))
  );
}

} // namespace llbuild3
