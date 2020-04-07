//===- EvoEngine.h ----------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_EVO_EVOENGINE_H
#define LLBUILD_EVO_EVOENGINE_H

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/Subprocess.h"

#include "llbuild/Core/BuildEngine.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llbuild {
namespace basic {
  class ExecutionQueue;
  class FileSystem;
}

namespace evo {

class EvoEngine;

class EvoRule : public core::Rule {
public:
  EvoRule(const core::KeyType& key,
          const basic::CommandSignature& signature = {})
    : Rule(key, signature) { }
  virtual ~EvoRule() = 0;

  /// Run the rule, producing the update to date value for it.
  virtual core::ValueType run(EvoEngine&) = 0;

private:
  // EvoRule manages the task creation automatically
  core::Task* createTask(core::BuildEngine&) override;
};


struct EvoInputHandle_t;
typedef EvoInputHandle_t* EvoInputHandle;

struct EvoProcessHandle_t;
typedef EvoProcessHandle_t* EvoProcessHandle;
struct EvoProcessResult {
  basic::ProcessResult proc{};
  std::string output;
  std::vector<std::string> errors;
};

struct EvoTaskHandle_t;
typedef EvoTaskHandle_t* EvoTaskHandle;
typedef std::function<core::ValueType&&()> EvoTaskFn;


class EvoEngine {
public:
  virtual ~EvoEngine() = 0;

  virtual EvoInputHandle request(const core::KeyType& key) = 0;
  virtual EvoProcessHandle spawn(
    ArrayRef<StringRef> commandLine,
    ArrayRef<std::pair<StringRef, StringRef>> environment,
    basic::ProcessAttributes attributes = {true}
  ) = 0;
  virtual EvoTaskHandle spawn(StringRef description, EvoTaskFn work) = 0;

  virtual const core::ValueType& wait(EvoInputHandle) = 0;
  virtual const EvoProcessResult& wait(EvoProcessHandle) = 0;
  virtual const core::ValueType& wait(EvoTaskHandle) = 0;
};

}
}

#endif
