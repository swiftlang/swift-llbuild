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

#ifndef TRITIUM_CORE_SWIFTADAPTORS_H
#define TRITIUM_CORE_SWIFTADAPTORS_H

#include <memory>
#include <optional>
#include <string>

#include <Tritium/Visibility.hpp>
#include <Tritium/Result.hpp>


namespace tritium {
namespace core {

class Build;
class Engine;
struct EngineConfig;

// Serialized Protobuf Objects
typedef std::string ArtifactPB;
typedef std::string ErrorPB;
typedef std::string LabelPB;
typedef std::string SignaturePB;
typedef std::string TaskContextPB;
typedef std::string TaskInputsPB;
typedef std::string TaskNextStatePB;


// Swift helper typedefs
typedef std::vector<LabelPB> LabelVector;

// External Adaptor Objects

struct ExtRule;

struct ExtRuleProvider {
  void* ctx;

  // FIXME: some method for cleaning up context

  void (*rulePrefixesFn)(void*, std::vector<LabelPB>*);
  void (*artifactPrefixesFn)(void*, std::vector<LabelPB>*);

  bool (*ruleByNameFn)(void*, const LabelPB*, ExtRule*);
  bool (*ruleForArtifactFn)(void*, const LabelPB*, ExtRule*);
};

class ExtTaskInterface {
private:
  void* impl;
  uint64_t ctx;

public:
  ExtTaskInterface(void* impl, uint64_t ctx) : impl(impl), ctx(ctx) { }

  TRITIUM_EXPORT ErrorPB registerRuleProvider(const ExtRuleProvider provider);

  TRITIUM_EXPORT result<uint64_t, ErrorPB> requestArtifact(const LabelPB label);
  TRITIUM_EXPORT result<uint64_t, ErrorPB> requestRule(const LabelPB label);
  TRITIUM_EXPORT result<uint64_t, ErrorPB> requestAction();
};

struct ExtTask {
  void* ctx;

  LabelPB name;

  bool isInit = false;

  // FIXME: some method for cleaning up context

  void (*producesFn)(void*, std::vector<LabelPB>*);
  bool (*computeFn)(void*, ExtTaskInterface, const TaskContextPB*, const TaskInputsPB*, TaskNextStatePB*);
};

struct ExtRule {
  void* ctx;

  LabelPB name;
  SignaturePB signature;

  // FIXME: some method for cleaning up context

  void (*producesFn)(void*, std::vector<LabelPB>*);

  bool (*configureTaskFn)(void*, ExtTask*);
};


// Copyable Reference Objects
class BuildRef {
  std::shared_ptr<Build> build;
public:
  BuildRef(std::shared_ptr<Build> build) : build(build) { }

  TRITIUM_EXPORT void cancel();
  TRITIUM_EXPORT void addCompletionHandler(void* ctx, void (*handler)(void*, result<ArtifactPB, ErrorPB>*));
};

struct ExtEngineConfig {
  std::optional<LabelPB> initRule;

  TRITIUM_EXPORT inline void setInitRule(LabelPB ir) { initRule = ir; }
};

class EngineRef {
  std::shared_ptr<Engine> engine;
public:
  EngineRef(std::shared_ptr<Engine> engine) : engine(engine) { }

  TRITIUM_EXPORT BuildRef build(const LabelPB artifact);
};

TRITIUM_EXPORT EngineRef makeEngine(ExtEngineConfig config, /* action cache, ... */ const ExtRuleProvider provider);

}
}

#endif /* Header_h */
