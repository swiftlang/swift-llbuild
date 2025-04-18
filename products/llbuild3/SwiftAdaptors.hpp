//===- SwiftAdaptors.hpp ----------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD3_SWIFTADAPTORS_H
#define LLBUILD3_SWIFTADAPTORS_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <llbuild3/Visibility.hpp>
#include <llbuild3/Result.hpp>


namespace llbuild3 {

class Build;
class Engine;
struct EngineConfig;

// Serialized Protobuf Objects
typedef std::string ActionPB;
typedef std::string ActionResultPB;
typedef std::string ArtifactPB;
typedef std::string CacheKeyPB;
typedef std::string CacheValuePB;
typedef std::string CASIDBytes;
typedef std::string CASObjectPB;
typedef std::string EngineIDBytes;
typedef std::string ErrorPB;
typedef std::string FileObjectPB;
typedef std::string LabelPB;
typedef std::string PlatformPB;
typedef std::string RemoteActionIDBytes;
typedef std::string SignaturePB;
typedef std::string StatPB;
typedef std::string TaskContextPB;
typedef std::string TaskInputsPB;
typedef std::string TaskNextStatePB;


// Swift helper typedefs
typedef std::vector<LabelPB> LabelVector;
typedef std::vector<std::pair<uint64_t, result<void*, ErrorPB>>> SubtaskResultMap;

// External Adaptor Objects

class ClientContext;
class Logger;
typedef std::shared_ptr<ClientContext> ClientContextRef;
typedef std::shared_ptr<Logger> LoggerRef;

LLBUILD3_EXPORT ClientContextRef makeExtClientContext(void* ctx,
                                                      void (*)(void* ctx));

struct ExtLoggerContext {
  void* ctx = nullptr;
  EngineIDBytes engineID;
};

struct ExtLogger {
  void* ctx;

  void (*releaseFn)(void* ctx);
  void (*errorFn)(void* ctx, ExtLoggerContext, ErrorPB);
  void (*eventFn)(void* ctx, ExtLoggerContext, std::vector<StatPB>*);
};

LLBUILD3_EXPORT LoggerRef makeExtLogger(ExtLogger);


class CASDatabase;
typedef std::shared_ptr<CASDatabase> CASDatabaseRef;

struct ExtCASDatabase {
  void* ctx;

  void (*releaseFn)(void* ctx);

  void (*containsFn)(void* ctx, CASIDBytes id, std::function<void (bool, ErrorPB)>);
  void (*getFn)(void* ctx, CASIDBytes id, std::function<void (CASObjectPB, ErrorPB)>);
  void (*putFn)(void* ctx, CASObjectPB obj, std::function<void (CASIDBytes, ErrorPB)>);
  CASIDBytes (*identifyFn)(void* ctx, CASObjectPB obj);
};

LLBUILD3_EXPORT CASDatabaseRef makeExtCASDatabase(ExtCASDatabase extCASDB);
LLBUILD3_EXPORT CASDatabaseRef makeInMemoryCASDatabase();

LLBUILD3_EXPORT void* getRawCASDatabaseContext(CASDatabaseRef casDB);
LLBUILD3_EXPORT void adaptedCASDatabaseContains(CASDatabaseRef casDB, CASIDBytes, void* ctx, void (*handler)(void*, result<bool, ErrorPB>*));
LLBUILD3_EXPORT void adaptedCASDatabaseGet(CASDatabaseRef casDB, CASIDBytes, void* ctx, void (*handler)(void*, result<CASObjectPB, ErrorPB>*));
LLBUILD3_EXPORT void adaptedCASDatabasePut(CASDatabaseRef casDB, CASObjectPB, void* ctx, void (*handler)(void*, result<CASIDBytes, ErrorPB>*));
LLBUILD3_EXPORT CASIDBytes adaptedCASDatabaseIdentify(CASDatabaseRef casDB, CASObjectPB);

struct ExtRule;

struct ExtRuleProvider {
  void* ctx;

  void (*releaseFn)(void* ctx);

  void (*rulePrefixesFn)(void*, std::vector<LabelPB>*);
  void (*artifactPrefixesFn)(void*, std::vector<LabelPB>*);

  bool (*ruleByNameFn)(void*, const LabelPB*, ExtRule*);
  bool (*ruleForArtifactFn)(void*, const LabelPB*, ExtRule*);
};

class ExtSubtaskInterface {
private:
  void* impl;
  uint64_t ctx;

public:
  ExtSubtaskInterface(void* impl, uint64_t ctx) : impl(impl), ctx(ctx) { }

  LLBUILD3_EXPORT CASDatabaseRef cas();
};

struct ExtSubtask {
  void* ctx;

  void (*perform)(void*, ExtSubtaskInterface, std::function<void (void*, ErrorPB)>);
};

class ExtTaskInterface {
private:
  void* impl;
  uint64_t ctx;

public:
  ExtTaskInterface(void* impl, uint64_t ctx) : impl(impl), ctx(ctx) { }

  LLBUILD3_EXPORT ErrorPB registerRuleProvider(const ExtRuleProvider provider);

  LLBUILD3_EXPORT result<uint64_t, ErrorPB> requestArtifact(const LabelPB label);
  LLBUILD3_EXPORT result<uint64_t, ErrorPB> requestRule(const LabelPB label);
  LLBUILD3_EXPORT result<uint64_t, ErrorPB> requestAction(const ActionPB action);
  LLBUILD3_EXPORT result<uint64_t, ErrorPB> spawnSubtask(const ExtSubtask subtask);
};

struct ExtTask {
  void* ctx;

  LabelPB name;
  SignaturePB signature;

  bool isInit = false;

  void (*releaseFn)(void* ctx);

  void (*producesFn)(void*, std::vector<LabelPB>*);
  bool (*computeFn)(void*, ExtTaskInterface, const TaskContextPB*, const TaskInputsPB*, SubtaskResultMap*, TaskNextStatePB*);
};

struct ExtRule {
  void* ctx;

  LabelPB name;
  SignaturePB signature;

  void (*releaseFn)(void* ctx);

  void (*producesFn)(void*, std::vector<LabelPB>*);

  bool (*configureTaskFn)(void*, ExtTask*);
};


// Copyable Reference Objects
class BuildRef {
  std::shared_ptr<Build> build;
public:
  BuildRef(std::shared_ptr<Build> build) : build(build) { }

  LLBUILD3_EXPORT void cancel();
  LLBUILD3_EXPORT void addCompletionHandler(void* ctx, void (*handler)(void*, result<ArtifactPB, ErrorPB>*));
};

struct ExtActionCache {
  void* ctx;

  void (*releaseFn)(void* ctx);

  void (*getFn)(void* ctx, CacheKeyPB key, std::function<void (CacheValuePB, ErrorPB)>);
  void (*updateFn)(void*, CacheKeyPB key, CacheValuePB value);
};

class ActionCache;
typedef std::shared_ptr<ActionCache> ActionCacheRef;
LLBUILD3_EXPORT ActionCacheRef makeExtActionCache(ExtActionCache extCache);
LLBUILD3_EXPORT ActionCacheRef makeInMemoryActionCache();


struct ExtActionDescriptor {
  LabelPB name;
  PlatformPB platform;
  std::string executable;
};

struct ExtActionProvider {
  void* ctx;

  void (*releaseFn)(void* ctx);

  void (*prefixesFn)(void*, std::vector<LabelPB>*);
  LabelPB (*resolveFn)(void*, LabelPB, ErrorPB*);
  ExtActionDescriptor (*descriptorFn)(void*, LabelPB, ErrorPB*);
};

struct ExtLocalSandbox {
  void* ctx;

  void (*releaseFn)(void* ctx);

  void (*dirFn)(void* ctx, std::string*);
  void (*envFn)(void* ctx, std::vector<std::pair<std::string, std::string>>*);
  void (*prepareInputFn)(void* ctx, std::string* path, int type, CASIDBytes* id, ErrorPB*);
  void (*collectOutputsFn)(void*, std::vector<std::string>, std::vector<FileObjectPB>*, ErrorPB*);
  void (*releaseSandboxFn)(void* ctx);
};

inline std::pair<std::string, std::string> makeStringPair(std::string l, std::string r) {
  return std::make_pair(l, r);
}

struct ExtLocalSandboxProvider {
  void* ctx;

  void (*releaseFn)(void* ctx);

  ExtLocalSandbox (*createFn)(void* ctx, uint64_t handle, ErrorPB*);
};

struct ExtRemoteExecutor {
  void* ctx;

  void (*releaseFn)(void* ctx);

  std::string builtinExecutable;

  void (*prepareFn)(void* ctx, std::string path,
                    std::function<void(CASIDBytes, ErrorPB)>);
  void (*executeFn)(void* ctx, CASIDBytes id, ActionPB,
                    std::function<void(RemoteActionIDBytes, ErrorPB)>,
                    std::function<void(ActionResultPB, ErrorPB)>);
};

class ActionExecutor;
class ActionProvider;
class LocalExecutor;
class LocalSandboxProvider;
class RemoteExecutor;
typedef std::shared_ptr<ActionExecutor> ActionExecutorRef;
typedef std::shared_ptr<ActionProvider> ActionProviderRef;
typedef std::shared_ptr<LocalExecutor> LocalExecutorRef;
typedef std::shared_ptr<LocalSandboxProvider> LocalSandboxProviderRef;
typedef std::shared_ptr<RemoteExecutor> RemoteExecutorRef;
LLBUILD3_EXPORT ActionExecutorRef makeActionExecutor(CASDatabaseRef, ActionCacheRef, LocalExecutorRef, RemoteExecutorRef, LoggerRef);
LLBUILD3_EXPORT ActionProviderRef makeExtActionProvider(ExtActionProvider);
LLBUILD3_EXPORT ErrorPB registerProviderWithExecutor(ActionExecutorRef, ActionProviderRef);
LLBUILD3_EXPORT LocalSandboxProviderRef makeExtLocalSandboxProvider(ExtLocalSandboxProvider);
LLBUILD3_EXPORT LocalExecutorRef makeLocalExecutor(LocalSandboxProviderRef);
LLBUILD3_EXPORT RemoteExecutorRef makeRemoteExecutor(ExtRemoteExecutor);

struct ExtEngineConfig {
  std::optional<LabelPB> initRule;

  LLBUILD3_EXPORT inline void setInitRule(LabelPB ir) { initRule = ir; }
};

class EngineRef {
  std::shared_ptr<Engine> engine;
public:
  EngineRef(std::shared_ptr<Engine> engine) : engine(engine) { }

  LLBUILD3_EXPORT CASDatabaseRef cas();

  LLBUILD3_EXPORT BuildRef build(const LabelPB artifact);

  LLBUILD3_EXPORT std::vector<StatPB> stats();
};

LLBUILD3_EXPORT EngineRef makeEngine(ExtEngineConfig config,
                                     CASDatabaseRef casdb,
                                     ActionCacheRef cache,
                                     ActionExecutorRef executor,
                                     LoggerRef logger,
                                     ClientContextRef clientContext,
                                     const ExtRuleProvider provider);

}

#endif /* Header_h */
