//===-- NinjaCommand.cpp --------------------------------------------------===//
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

#include "llbuild/Commands/Commands.h"

#include "llbuild/Basic/JSON.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Ninja/Lexer.h"
#include "llbuild/Ninja/Manifest.h"
#include "llbuild/Ninja/ManifestLoader.h"
#include "llbuild/Ninja/Parser.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"

#include "CommandUtil.h"
#include "NinjaBuildCommand.h"

#include <cstdio>
#include <algorithm>
#include <iostream>
#include <iomanip>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::commands;

static void usage() {
  fprintf(stderr, "Usage: %s ninja [--help] <command> [<args>]\n",
          getProgramName());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  build         -- Build using Ninja manifests\n");
  fprintf(stderr, "  lex           -- Run the Ninja lexer\n");
  fprintf(stderr, "  parse         -- Run the Ninja parser\n");
  fprintf(stderr, "  load-manifest -- Load a Ninja manifest file\n");
  fprintf(stderr, "\n");
  exit(1);
}

static std::string escapedString(const ninja::Token& token) {
  return util::escapedString(StringRef(token.start, token.length));
}

#pragma mark - Lex Command

static int executeLexCommand(const std::vector<std::string> &args,
                             bool lexOnly) {

  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    return 1;
  }

  // Read the input.
  auto bufferOrError = util::readFileContents(args[0]);
  if (!bufferOrError) {
    fprintf(stderr, "error: %s: %s\n", getProgramName(),
            llvm::toString(bufferOrError.takeError()).c_str());
    exit(1);
  }

  // Create a Ninja lexer.
  if (!lexOnly) {
      fprintf(stderr, "note: %s: reading tokens from %s\n", getProgramName(),
              args[0].c_str());
  }
  ninja::Lexer lexer(bufferOrError.get()->getBuffer());
  ninja::Token tok;

  do {
    // Get the next token.
    lexer.lex(tok);

    if (!lexOnly) {
        std::cerr << "(Token \"" << tok.getKindName() << "\""
                  << " String:\"" << escapedString(tok) << "\""
                  << " Length:" << tok.length
                  << " Line:" << tok.line
                  << " Column:" << tok.column << ")\n";
    }
  } while (tok.tokenKind != ninja::Token::Kind::EndOfFile);

  return 0;
}

#pragma mark - Parse Command

namespace {

class ParseCommandActions : public ninja::ParseActions {
  std::string filename;
  unsigned numErrors = 0;
  unsigned maxErrors = 20;
  ninja::Parser* parser = 0;

public:
  ParseCommandActions(std::string filename) : filename(filename) {}

private:
  virtual void initialize(ninja::Parser* parser) override {
    this->parser = parser;
  }

  virtual void error(StringRef message, const ninja::Token& at) override {
    if (numErrors++ >= maxErrors) {
      return;
    }

    util::emitError(filename, message, at, parser);
  }

  virtual void actOnBeginManifest(StringRef name) override {
    std::cerr << __FUNCTION__ << "(\"";
    std::cerr.write(name.data(), name.size());
    std::cerr << "\")\n";
  }

  virtual void actOnEndManifest() override {
    std::cerr << __FUNCTION__ << "()\n";
  }

  virtual void actOnBindingDecl(const ninja::Token& name,
                                const ninja::Token& value) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(name) << "\", "
              << "/*Value=*/\"" << escapedString(value) << "\")\n";
  }

  virtual void actOnDefaultDecl(ArrayRef<ninja::Token> names) override {
    std::cerr << __FUNCTION__ << "(/*Names=*/[";
    bool first = true;
    for (auto& name: names) {
      if (!first)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(name) << "\"";
      first = false;
    }
    std::cerr << "])\n";
  }

  virtual void actOnIncludeDecl(bool isInclude,
                                const ninja::Token& path) override {
    std::cerr << __FUNCTION__ << "(/*IsInclude=*/"
              << (isInclude ? "true" : "false") << ", "
              << "/*Path=*/\"" << escapedString(path)
              << "\")\n";
  }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& name,
                      ArrayRef<ninja::Token> outputs,
                      ArrayRef<ninja::Token> inputs,
                      unsigned numExplicitInputs,
                      unsigned numImplicitInputs) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(name) << "\""
              << ", /*Outputs=*/[";
    bool first = true;
    for (auto& name: outputs) {
      if (!first)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(name) << "\"";
      first = false;
    }
    std::cerr << "], /*Inputs=*/[";
    first = true;
    for (auto& name: inputs) {
      if (!first)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(name) << "\"";
      first = false;
    }
    std::cerr << "], /*NumExplicitInputs=*/" << numExplicitInputs
              << ", /*NumImplicitInputs=*/"  << numImplicitInputs << ")\n";
    return 0;
  }

  virtual void actOnBuildBindingDecl(BuildResult decl, const ninja::Token& name,
                                     const ninja::Token& value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ", /*Name=*/"
              << "\"" << escapedString(name) << "\", "
              << "/*Value=*/\"" << escapedString(value) << "\")\n";
  }

  virtual void actOnEndBuildDecl(BuildResult decl,
                                 const ninja::Token& start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ")\n";
  }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(name) << "\")\n";
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult decl, const ninja::Token& name,
                                     const ninja::Token& value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ", /*Name=*/"
              << "\"" << escapedString(name) << "\", "
              << "/*Value=*/\"" << escapedString(value) << "\")\n";
  }

  virtual void actOnEndPoolDecl(PoolResult decl,
                                const ninja::Token& start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ")\n";
  }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(name) << "\")\n";
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult decl, const ninja::Token& name,
                                     const ninja::Token& value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ", /*Name=*/"
              << "\"" << escapedString(name) << "\", "
              << "/*Value=*/\"" << escapedString(value) << "\")\n";
  }

  virtual void actOnEndRuleDecl(RuleResult decl,
                                const ninja::Token& start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ")\n";
  }
};

}

#pragma mark - Parse Only Command

namespace {

class ParseOnlyCommandActions : public ninja::ParseActions {
  ninja::Parser* parser = 0;

public:
  ParseOnlyCommandActions() {}

private:
  virtual void initialize(ninja::Parser* parser) override {
    this->parser = parser;
  }

  virtual void error(StringRef message, const ninja::Token& at) override { }

  virtual void actOnBeginManifest(StringRef name) override { }

  virtual void actOnEndManifest() override { }

  virtual void actOnBindingDecl(const ninja::Token& name,
                                const ninja::Token& value) override { }

  virtual void actOnDefaultDecl(ArrayRef<ninja::Token> names) override { }

  virtual void actOnIncludeDecl(bool isInclude,
                                const ninja::Token& path) override { }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& name,
                      ArrayRef<ninja::Token> outputs,
                      ArrayRef<ninja::Token> inputs,
                      unsigned numExplicitInputs,
                      unsigned numImplicitInputs) override {
    return 0;
  }

  virtual void actOnBuildBindingDecl(BuildResult decl, const ninja::Token& name,
                                     const ninja::Token& value) override { }

  virtual void actOnEndBuildDecl(BuildResult decl,
                                 const ninja::Token& start) override { }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& name) override {
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult decl, const ninja::Token& name,
                                    const ninja::Token& value) override { }

  virtual void actOnEndPoolDecl(PoolResult decl,
                                const ninja::Token& start) override { }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& name) override {
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult decl, const ninja::Token& name,
                                    const ninja::Token& value) override { }

  virtual void actOnEndRuleDecl(RuleResult decl,
                                 const ninja::Token& start) override { }
};

}

static int executeParseCommand(const std::vector<std::string> &args,
                               bool parseOnly) {
  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    return 1;
  }

  // Read the input.
  auto bufferOrError = util::readFileContents(args[0]);
  if (!bufferOrError) {
    fprintf(stderr, "error: %s: %s\n", getProgramName(),
            llvm::toString(bufferOrError.takeError()).c_str());
    exit(1);
  }

  // Run the parser.
  if (parseOnly) {
      ParseOnlyCommandActions actions;
      ninja::Parser parser(bufferOrError.get()->getBuffer(), actions);
      parser.parse();
  } else {
      ParseCommandActions actions(args[0]);
      ninja::Parser parser(bufferOrError.get()->getBuffer(), actions);
      parser.parse();
  }

  return 0;
}

#pragma mark - Load Manifest Command

namespace {

class LoadManifestActions : public ninja::ManifestLoaderActions {
  ninja::ManifestLoader *Loader = 0;
  unsigned NumErrors = 0;
  unsigned MaxErrors = 20;

private:
  virtual void initialize(ninja::ManifestLoader *Loader) override {
    this->Loader = Loader;
  }

  virtual void error(StringRef filename, StringRef message,
                     const ninja::Token& at) override {
    if (NumErrors++ >= MaxErrors)
      return;

    util::emitError(filename, message, at, Loader->getCurrentParser());
  }

  virtual std::unique_ptr<llvm::MemoryBuffer> readFileContents(
      StringRef filename, StringRef forFilename,
      const ninja::Token* forToken) override {
    auto bufferOrError = util::readFileContents(filename);
    if (bufferOrError)
      return std::move(*bufferOrError);

    std::string error = llvm::toString(bufferOrError.takeError());
    ++NumErrors;
    if (forToken) {
      util::emitError(forFilename, error, *forToken,
                      Loader->getCurrentParser());
    } else {
      // We were unable to open the main file.
      fprintf(stderr, "error: %s: %s\n", getProgramName(), error.c_str());
      exit(1);
    }

    return nullptr;
  }
};

}

static void dumpNinjaManifestText(StringRef file, ninja::Manifest* manifest);
static void dumpNinjaManifestJSON(StringRef file, ninja::Manifest* manifest);

static int executeLoadManifestCommand(const std::vector<std::string>& args,
                                      bool loadOnly) {
  // Parse options.
  bool json = false;
  auto it = args.begin();
  for (; it != args.end() && StringRef(*it).startswith("-"); ++it) {
    auto arg = *it;

    if (arg == "--json") {
      json = true;
    } else {
      fprintf(stderr, "error: %s: unknown option: '%s'\n",
              getProgramName(), arg.c_str());
      return 1;
    }
  }
  if (it == args.end()) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    return 1;
  }

  std::string filename = *it++;
  if (it != args.end()) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    return 1;
  }

  // Change to the directory containing the input file, so include references
  // can be relative.
  //
  // FIXME: Need llvm::sys::fs.
  size_t pos = filename.find_last_of('/');
  if (pos != std::string::npos) {
    if (!sys::chdir(filename.substr(0, pos).c_str())) {
      fprintf(stderr, "error: %s: unable to chdir(): %s\n",
              getProgramName(), sys::strerror(errno).c_str());
      return 1;
    }
    filename = filename.substr(pos+1);
  }

  SmallString<128> current_dir;
  if (std::error_code ec = llvm::sys::fs::current_path(current_dir)) {
    fprintf(stderr, "%s: error: cannot determine current directory\n",
            getProgramName());
    return 1;
  }
  const std::string workingDirectory = current_dir.str();

  LoadManifestActions actions;
  ninja::ManifestLoader loader(workingDirectory, filename, actions);
  std::unique_ptr<ninja::Manifest> manifest = loader.load();

  // If only loading, we are done.
  if (loadOnly)
    return 0;

  if (json) {
    dumpNinjaManifestJSON(filename, manifest.get());
  } else {
    dumpNinjaManifestText(filename, manifest.get());
  }

  return 0;
}

static void dumpNinjaManifestText(StringRef file, ninja::Manifest* manifest) {
  // Dump the manifest.
  std::cout << "# Loaded Manifest: \"" << file.str() << "\"\n";
  std::cout << "\n";

  // Dump the top-level bindings.
  std::cout << "# Top-Level Bindings\n";
  assert(manifest->getRootScope().getParent() == nullptr);
  std::vector<std::pair<std::string, std::string>> bindings;
  for (const auto& entry: manifest->getRootScope().getBindings()) {
    bindings.push_back({ entry.getKey(), entry.getValue() });
  }
  std::sort(bindings.begin(), bindings.end());
  for (const auto& entry: bindings) {
    std::cout << entry.first << " = \""
              << util::escapedString(entry.second) << "\"\n";
  }
  std::cout << "\n";

  // Dump the pools, if present.
  if (!manifest->getPools().empty()) {
    std::cout << "# Pools\n";
    std::vector<ninja::Pool*> pools;
    for (const auto& entry: manifest->getPools()) {
      pools.push_back(entry.getValue());
    }
    std::sort(pools.begin(), pools.end(), [] (ninja::Pool* a, ninja::Pool* b) {
        return a->getName() < b->getName();
      });
    for (const auto& pool: pools) {
      // Write the rule entry.
      std::cout << "pool " << pool->getName() << "\n";
      if (uint32_t depth = pool->getDepth()) {
        std::cout << "  depth = " << depth << "\n";
      }
      std::cout << "\n";
    }
  }

  // Dump the rules.
  std::cout << "# Rules\n";
  std::vector<ninja::Rule*> rules;
  for (const auto& entry: manifest->getRootScope().getRules()) {
    rules.push_back(entry.getValue());
  }
  std::sort(rules.begin(), rules.end(), [] (ninja::Rule* a, ninja::Rule* b) {
      return a->getName() < b->getName();
    });
  for (const auto& rule: rules) {
    // Write the rule entry.
    std::cout << "rule " << rule->getName() << "\n";

    // Write the parameters.
    std::vector<std::pair<std::string, std::string>> parameters;
    for (const auto& entry: rule->getParameters()) {
        parameters.push_back({ entry.getKey(), entry.getValue() });
    }
    std::sort(parameters.begin(), parameters.end());
    for (const auto& entry: parameters) {
      std::cout << "  " << entry.first << " = \""
                << util::escapedString(entry.second) << "\"\n";
    }
    std::cout << "\n";
  }

  // Dump the commands.
  std::vector<ninja::Command*> commands(manifest->getCommands());
  std::sort(commands.begin(), commands.end(),
            [] (ninja::Command* a, ninja::Command* b) {
              // Commands can not have duplicate outputs, so comparing based
              // only on the first still provides a total ordering.
              return a->getOutputs()[0]->getScreenPath() <
                b->getOutputs()[0]->getScreenPath();
            });
  std::cout << "# Commands\n";
  for (const auto& command: commands) {
    // Write the command entry.
    std::cout << "build";
    for (const auto& node: command->getOutputs()) {
      std::cout << " \"" << util::escapedString(node->getScreenPath()) << "\"";
    }
    std::cout << ": " << command->getRule()->getName();
    unsigned count = 0;
    for (const auto& node: command->getInputs()) {
      std::cout << " ";
      if (count == (command->getNumExplicitInputs() +
                           command->getNumImplicitInputs())) {
        std::cout << "|| ";
      } else if (count == command->getNumExplicitInputs()) {
        std::cout << "| ";
      }
      std::cout << "\"" << util::escapedString(node->getScreenPath()) << "\"";
      ++count;
    }
    std::cout << "\n";

    // Write out the attributes.
    std::cout << "  command = \""
              << util::escapedString(command->getCommandString()) << "\"\n";
    std::cout << "  description = \""
              << util::escapedString(command->getDescription()) << "\"\n";

    switch (command->getDepsStyle()) {
    case ninja::Command::DepsStyleKind::None:
      break;
    case ninja::Command::DepsStyleKind::GCC:
      std::cout << "  deps = gcc\n";
      std::cout << "  depfile = \""
                << util::escapedString(command->getDepsFile()) << "\"\n";
      break;
    case ninja::Command::DepsStyleKind::MSVC:
      std::cout << "  deps = msvc\n";
      break;
    }
  
    if (command->hasGeneratorFlag())
      std::cout << "  generator = 1\n";
    if (command->hasRestatFlag())
      std::cout << "  restat = 1\n";

    if (const ninja::Pool* executionPool = command->getExecutionPool()) {
      std::cout << "  pool = " << executionPool->getName() << "\n";
    }

    std::cout << "\n";
  }

  // Dump the default targets, if specified.
  if (!manifest->getDefaultTargets().empty()) {
    std::cout << "# Default Targets\n";
    std::vector<ninja::Node*> defaultTargets = manifest->getDefaultTargets();
    std::sort(defaultTargets.begin(), defaultTargets.end(),
              [] (ninja::Node* a, ninja::Node* b) {
        return a->getScreenPath() < b->getScreenPath();
      });
    std::cout << "default ";
    for (const auto& node: defaultTargets) {
      if (node != defaultTargets[0])
        std::cout << " ";
      std::cout << "\"" << node->getScreenPath() << "\"";
    }
    std::cout << "\n\n";
  }
}

static void dumpNinjaManifestJSON(StringRef file, ninja::Manifest* manifest) {
  std::cout << "{\n";
  std::cout << "  \"filename\": \"" << escapeForJSON(file) << "\",\n";

  // Dump the top-level bindings.
  assert(manifest->getRootScope().getParent() == nullptr);
  std::vector<std::pair<std::string, std::string>> bindings;
  for (const auto& entry: manifest->getRootScope().getBindings()) {
    bindings.push_back({ entry.getKey(), entry.getValue() });
  }
  std::sort(bindings.begin(), bindings.end());
  std::cout << "  \"bindings\": {\n";
  for (auto it = bindings.begin(), ie = bindings.end(); it != ie; ++it) {
    if (it != bindings.begin()) std::cout << ",\n";
    std::cout << "    \"" << escapeForJSON(it->first) << "\": \""
              << escapeForJSON(it->second) << "\"";
  }
  std::cout << "\n  },\n";
  
  // Dump the pools, if present.
  if (!manifest->getPools().empty()) {
    std::vector<ninja::Pool*> pools;
    for (const auto& entry: manifest->getPools()) {
      pools.push_back(entry.getValue());
    }
    std::sort(pools.begin(), pools.end(), [] (ninja::Pool* a, ninja::Pool* b) {
        return a->getName() < b->getName();
      });
    std::cout << "  \"pools\": {\n";
    for (auto it = pools.begin(), ie = pools.end(); it != ie; ++it) {
      auto pool = *it;
      if (it != pools.begin()) std::cout << ",\n";
      std::cout << "    \"" << escapeForJSON(pool->getName()) << "\": {";
      if (uint32_t depth = pool->getDepth()) {
        std::cout << "\n      \"depth\": " << depth;
      }
      std::cout << "\n    }";
    }
    std::cout << "\n  },\n";
  }

  // Dump the rules.
  std::vector<ninja::Rule*> rules;
  for (const auto& entry: manifest->getRootScope().getRules()) {
    rules.push_back(entry.getValue());
  }
  std::sort(rules.begin(), rules.end(), [] (ninja::Rule* a, ninja::Rule* b) {
      return a->getName() < b->getName();
    });
  std::cout << "  \"rules\": {\n";
  for (auto it = rules.begin(), ie = rules.end(); it != ie; ++it) {
    auto rule = *it;
    if (it != rules.begin()) std::cout << ",\n";
    std::cout << "    \"" << escapeForJSON(rule->getName()) << "\": {\n";

    std::vector<std::pair<std::string, std::string>> parameters;
    for (const auto& entry: rule->getParameters()) {
        parameters.push_back({ entry.getKey(), entry.getValue() });
    }
    std::sort(parameters.begin(), parameters.end());
    for (auto pit = parameters.begin(),
           pie = parameters.end(); pit != pie; ++pit) {
      if (pit != parameters.begin()) std::cout << ",\n";
      std::cout << "      \"" << escapeForJSON(pit->first) << "\": \""
                << escapeForJSON(pit->second) << "\"";
    }
    std::cout << "\n    }";
  }
  std::cout << "\n  },\n";

  // Dump the default targets, if specified.
  if (!manifest->getDefaultTargets().empty()) {
    std::vector<ninja::Node*> defaultTargets = manifest->getDefaultTargets();
    std::sort(defaultTargets.begin(), defaultTargets.end(),
              [] (ninja::Node* a, ninja::Node* b) {
        return a->getScreenPath() < b->getScreenPath();
      });
    std::cout << "  \"default_targets\": [";
    for (auto it = defaultTargets.begin(),
           ie = defaultTargets.end(); it != ie; ++it) {
      if (it != defaultTargets.begin()) std::cout << ", ";
      std::cout << "\"" << escapeForJSON((*it)->getScreenPath()) << "\"";
    }
    std::cout << "],\n";
  }

  // Dump the commands.
  std::vector<ninja::Command*> commands(manifest->getCommands());
  std::sort(commands.begin(), commands.end(),
            [] (ninja::Command* a, ninja::Command* b) {
              // Commands can not have duplicate outputs, so comparing based
              // only on the first still provides a total ordering.
              return a->getOutputs()[0]->getScreenPath() <
                b->getOutputs()[0]->getScreenPath();
            });
  std::cout << "  \"commands\": [\n";
  for (auto it = commands.begin(), ie = commands.end(); it != ie; ++it) {
    auto command = *it;
    if (it != commands.begin()) std::cout << ",\n";
    std::cout << "    {\n";
    std::cout << "      \"outputs\": [";
    for (auto it = command->getOutputs().begin(),
           ie = command->getOutputs().end(); it != ie; ++it) {
      if (it != command->getOutputs().begin()) std::cout << ", ";
      std::cout << "\"" << escapeForJSON((*it)->getScreenPath()) << "\"";
    }
    std::cout << "],\n";
    std::cout << "      \"rule\": \""
              << command->getRule()->getName() << "\",\n";

    std::cout << "      \"inputs\": [";
    for (auto it = command->explicitInputs_begin(),
           ie = command->explicitInputs_end(); it != ie; ++it) {
      if (it != command->explicitInputs_begin()) std::cout << ", ";
      std::cout << "\"" << escapeForJSON((*it)->getScreenPath()) << "\"";
    }
    std::cout << "],\n";
    if (command->getNumImplicitInputs()) {
      std::cout << "      \"implicit_inputs\": [";
      for (auto it = command->implicitInputs_begin(),
             ie = command->implicitInputs_end(); it != ie; ++it) {
        if (it != command->implicitInputs_begin()) std::cout << ", ";
        std::cout << "\"" << escapeForJSON((*it)->getScreenPath()) << "\"";
      }
      std::cout << "],\n";
    }
    if (command->getNumOrderOnlyInputs()) {
      std::cout << "      \"order_only_inputs\": [";
      for (auto it = command->orderOnlyInputs_begin(),
             ie = command->orderOnlyInputs_end(); it != ie; ++it) {
        if (it != command->orderOnlyInputs_begin()) std::cout << ", ";
        std::cout << "\"" << escapeForJSON((*it)->getScreenPath()) << "\"";
      }
      std::cout << "],\n";
    }

    // Write out optional things.
    if (command->hasGeneratorFlag())
      std::cout << "      \"generator\": true,\n";
    if (command->hasRestatFlag())
      std::cout << "      \"restat\": true,\n";

    if (const ninja::Pool* executionPool = command->getExecutionPool()) {
      std::cout << "      \"pool\": \""
                << escapeForJSON(executionPool->getName()) << "\",\n";
    }

    switch (command->getDepsStyle()) {
    case ninja::Command::DepsStyleKind::None:
      break;
    case ninja::Command::DepsStyleKind::GCC:
      std::cout << "      \"deps\": \"gcc\",\n";
      std::cout << "      \"depfile\": \""
                << escapeForJSON(command->getDepsFile()) << "\",\n";
      break;
    case ninja::Command::DepsStyleKind::MSVC:
      std::cout << "      \"deps\": \"msvc\",\n";
      break;
    }
    
    // Finally, write out the attributes.
    std::cout << "      \"command\": \""
              << escapeForJSON(command->getCommandString()) << "\",\n";
    std::cout << "      \"description\": \""
              << escapeForJSON(command->getDescription()) << "\"\n";
    
    std::cout << "    }";
  }
  std::cout << "\n  ]\n";

  std::cout << "}\n";
}

#pragma mark - Ninja Top-Level Command

int commands::executeNinjaCommand(const std::vector<std::string>& args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (args.empty() || args[0] == "--help")
    usage();

  if (args[0] == "lex") {
    return executeLexCommand(std::vector<std::string>(args.begin()+1,
                                                      args.end()),
                             /*LexOnly=*/false);
  } else if (args[0] == "lex-only") {
    return executeLexCommand(std::vector<std::string>(args.begin()+1,
                                                      args.end()),
                             /*LexOnly=*/true);
  } else if (args[0] == "parse") {
    return executeParseCommand(std::vector<std::string>(args.begin()+1,
                                                        args.end()),
                               /*ParseOnly=*/false);
  } else if (args[0] == "parse-only") {
    return executeParseCommand(std::vector<std::string>(args.begin()+1,
                                                        args.end()),
                               /*ParseOnly=*/true);
  } else if (args[0] == "load-manifest") {
    return executeLoadManifestCommand(std::vector<std::string>(args.begin()+1,
                                                               args.end()),
                                      /*LoadOnly=*/false);
  } else if (args[0] == "load-manifest-only") {
    return executeLoadManifestCommand(std::vector<std::string>(args.begin()+1,
                                                               args.end()),
                                      /*LoadOnly=*/true);
  } else if (args[0] == "build") {
    return executeNinjaBuildCommand(std::vector<std::string>(args.begin()+1,
                                                             args.end()));
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getProgramName(),
            args[0].c_str());
    return 1;
  }
}
