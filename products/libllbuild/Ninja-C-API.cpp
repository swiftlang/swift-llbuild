//===-- Ninja-C-API.cpp ---------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include <llbuild/llbuild.h>
#include <type_traits>

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Ninja/Lexer.h"
#include "llbuild/Ninja/Manifest.h"
#include "llbuild/Ninja/ManifestLoader.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace llbuild;
using namespace llbuild::ninja;

static llb_string_ref_t toCRef(StringRef str) {
  return { str.size(), str.data() };
}

namespace {
class CAPIManifestActions : public ManifestLoaderActions {
  SmallVectorImpl<char> &Error;

public:
  CAPIManifestActions(SmallVectorImpl<char> &error) : Error(error) {}

  virtual void initialize(ninja::ManifestLoader *Loader) override {}

  virtual void error(StringRef path, StringRef message,
                     const Token &at) override {
    addError(message, path, at.line, at.column);
  }

  virtual std::unique_ptr<llvm::MemoryBuffer> readFile(
      StringRef path, StringRef forFilename,
      const Token *forToken) override {
    auto bufferOrError = llvm::MemoryBuffer::getFile(path);
    if (!bufferOrError) {
      auto ec = bufferOrError.getError();
      addError(ec.message(), forFilename);
      return nullptr;
    }
    return std::move(*bufferOrError);
  }

private:
  void addError(StringRef message, StringRef path) {
    llvm::raw_svector_ostream stream(Error);
    stream << path << ": " << message << "\n";
  }

  void addError(StringRef message, StringRef path, unsigned line,
                unsigned column) {
    llvm::raw_svector_ostream stream(Error);
    stream << path << ":" << line << ":" << column << ": " << message << "\n";
  }
};

class CAPIManifest {
  std::unique_ptr<Manifest> Underlying;
  SmallString<0> Error;
  llvm::BumpPtrAllocator Allocator;
  ArrayRef<llb_ninja_build_statement_t> Statements;
  ArrayRef<llb_string_ref_t> DefaultTargets;

  using RuleCacheTy = llvm::DenseMap<const Rule *, const llb_ninja_rule_t *>;

  template <typename Input, typename F>
  auto copyTransformed(const Input &orig, F &&transform) {
    using R = decltype(transform(*(orig.begin())));
    auto copied = llvm::makeMutableArrayRef(Allocator.Allocate<R>(orig.size()),
                                            orig.size());
    for (auto it : llvm::enumerate(orig)) {
      copied[it.index()] = transform(it.value());
    }
    return copied;
  }

  CAPIManifest() {}

public:
  static llb_ninja_manifest_t build(StringRef filename,
                                    StringRef workingDirectory) {
    auto manifest = new CAPIManifest();

    CAPIManifestActions actions(manifest->Error);
    ninja::ManifestLoader loader(workingDirectory, filename, actions);

    if (auto underlying = loader.load()) {
      manifest->Underlying = std::move(underlying);
      manifest->allocateRefs();
    }

    return {
      reinterpret_cast<llb_ninja_raw_manifest_t>(manifest),
      manifest->Statements.size(), manifest->Statements.data(),
      manifest->DefaultTargets.size(), manifest->DefaultTargets.data(),
      toCRef(manifest->Error)
    };
  }

private:
  void allocateRefs() {
    RuleCacheTy ruleCache;

    Statements = copyTransformed(
        Underlying->getCommands(),
        [&](const Command *statement) -> llb_ninja_build_statement_t {
      auto explicitInputs = copyRefs(statement->explicitInputs_begin(),
                                     statement->explicitInputs_end());
      auto implicitInputs = copyRefs(statement->implicitInputs_begin(),
                                     statement->implicitInputs_end());
      auto orderOnlyInputs = copyRefs(statement->orderOnlyInputs_begin(),
                                      statement->orderOnlyInputs_end());
      auto explicitOutputs = copyRefs(statement->explicitOutputs_begin(),
                                      statement->explicitOutputs_end());
      auto implicitOutputs = copyRefs(statement->implicitOutputs_begin(),
                                      statement->implicitOutputs_end());
      auto variables = copyRefs(statement->getParameters());

      return {
        copyRefs(statement->getRule(), ruleCache),
        toCRef(statement->getCommandString()),
        toCRef(statement->getDescription()),
        explicitInputs.size(), explicitInputs.data(),
        implicitInputs.size(), implicitInputs.data(),
        orderOnlyInputs.size(), orderOnlyInputs.data(),
        explicitOutputs.size(), explicitOutputs.data(),
        implicitOutputs.size(), implicitOutputs.data(),
        variables.size(), variables.data(),
        statement->hasGeneratorFlag(),
        statement->hasRestatFlag()
      };
    });
    DefaultTargets = copyRefs(Underlying->getDefaultTargets());
  }

  ArrayRef<llb_string_ref_t> copyRefs(ArrayRef<Node *> nodes) {
    return copyTransformed(nodes, [](auto &node) -> llb_string_ref_t {
      auto &path = node->getScreenPath();
      return { path.size(), path.data() };
    });
  }

  ArrayRef<llb_string_ref_t> copyRefs(
      const std::vector<Node*>::const_iterator &begin,
      const std::vector<Node*>::const_iterator &end) {
    return copyRefs(llvm::makeArrayRef(&*begin, &*end));
  }

  ArrayRef<llb_ninja_variable_t> copyRefs(
      const llvm::StringMap<std::string> &variables) {
    return copyTransformed(variables, [&](auto &var) -> llb_ninja_variable_t {
      return { toCRef(var.getKey()), toCRef(var.getValue()) };
    });
  }

  const llb_ninja_rule_t *copyRefs(const Rule *rule, RuleCacheTy &ruleCache) {
    const llb_ninja_rule_t *&cRule = ruleCache[rule];
    if (cRule)
      return cRule;

    auto cParams = copyRefs(rule->getParameters());
    return new (Allocator) llb_ninja_rule_t {
      toCRef(rule->getName()),
      cParams.size(), cParams.data()
    };
  }
};
}

llb_ninja_manifest_t llb_manifest_fs_load(
    const char *filename, const char *workingDirectory) {
  return CAPIManifest::build(filename, workingDirectory);
}

void llb_manifest_destroy(llb_ninja_manifest_t *manifest) {
  delete reinterpret_cast<CAPIManifest *>(manifest->raw_manifest);
}
