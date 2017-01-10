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

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Ninja/Lexer.h"
#include "llbuild/Ninja/Manifest.h"
#include "llbuild/Ninja/ManifestLoader.h"
#include "llbuild/Ninja/Parser.h"

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

#pragma mark - Lex Command

static int executeLexCommand(const std::vector<std::string> &args,
                             bool lexOnly) {

  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    return 1;
  }

  // Read the input.
  uint64_t size;
  std::unique_ptr<char[]> data;
  std::string error;
  if (!util::readFileContents(args[0], &data, &size, &error)) {
    fprintf(stderr, "error: %s: %s\n", getProgramName(), error.c_str());
    exit(1);
  }

  // Create a Ninja lexer.
  if (!lexOnly) {
      fprintf(stderr, "note: %s: reading tokens from %s\n", getProgramName(),
              args[0].c_str());
  }
  ninja::Lexer lexer(StringRef(data.get(), size));
  ninja::Token tok;

  do {
    // Get the next token.
    lexer.lex(tok);

    if (!lexOnly) {
        std::cerr << "(Token \"" << tok.getKindName() << "\""
                  << " String:\"" << util::escapedString(tok.start,
                                                         tok.length) << "\""
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

  virtual void error(std::string message, const ninja::Token& at) override {
    if (numErrors++ >= maxErrors) {
      return;
    }

    util::emitError(filename, message, at, parser);
  }

  virtual void actOnBeginManifest(std::string name) override {
    std::cerr << __FUNCTION__ << "(\"" << name << "\")\n";
  }

  virtual void actOnEndManifest() override {
    std::cerr << __FUNCTION__ << "()\n";
  }

  virtual void actOnBindingDecl(const ninja::Token& name,
                                const ninja::Token& value) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::escapedString(name.start, name.length) << "\", "
              << "/*Value=*/\"" << util::escapedString(value.start,
                                                       value.length) << "\")\n";
  }

  virtual void actOnDefaultDecl(ArrayRef<ninja::Token> names) override {
    std::cerr << __FUNCTION__ << "(/*Names=*/[";
    bool first = true;
    for (auto& name: names) {
      if (!first)
        std::cerr << ", ";
      std::cerr << "\"" << util::escapedString(name.start, name.length) << "\"";
      first = false;
    }
    std::cerr << "])\n";
  }

  virtual void actOnIncludeDecl(bool isInclude,
                                const ninja::Token& path) override {
    std::cerr << __FUNCTION__ << "(/*IsInclude=*/"
              << (isInclude ? "true" : "false") << ", "
              << "/*Path=*/\"" << util::escapedString(path.start, path.length)
              << "\")\n";
  }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& name,
                      ArrayRef<ninja::Token> outputs,
                      ArrayRef<ninja::Token> inputs,
                      unsigned numExplicitInputs,
                      unsigned numImplicitInputs) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::escapedString(name.start, name.length) << "\""
              << ", /*Outputs=*/[";
    bool first = true;
    for (auto& name: outputs) {
      if (!first)
        std::cerr << ", ";
      std::cerr << "\"" << util::escapedString(name.start, name.length) << "\"";
      first = false;
    }
    std::cerr << "], /*Inputs=*/[";
    first = true;
    for (auto& name: inputs) {
      if (!first)
        std::cerr << ", ";
      std::cerr << "\"" << util::escapedString(name.start, name.length) << "\"";
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
              << "\"" << util::escapedString(name.start, name.length) << "\", "
              << "/*Value=*/\"" << util::escapedString(value.start,
                                                       value.length) << "\")\n";
  }

  virtual void actOnEndBuildDecl(BuildResult decl,
                                 const ninja::Token& start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ")\n";
  }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::escapedString(name.start,
                                             name.length) << "\")\n";
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult decl, const ninja::Token& name,
                                     const ninja::Token& value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ", /*Name=*/"
              << "\"" << util::escapedString(name.start, name.length) << "\", "
              << "/*Value=*/\"" << util::escapedString(value.start,
                                                       value.length) << "\")\n";
  }

  virtual void actOnEndPoolDecl(PoolResult decl,
                                const ninja::Token& start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ")\n";
  }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::escapedString(name.start,
                                             name.length) << "\")\n";
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult decl, const ninja::Token& name,
                                     const ninja::Token& value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(decl) << ", /*Name=*/"
              << "\"" << util::escapedString(name.start, name.length) << "\", "
              << "/*Value=*/\"" << util::escapedString(value.start,
                                                       value.length) << "\")\n";
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

  virtual void error(std::string message, const ninja::Token& at) override { }

  virtual void actOnBeginManifest(std::string name) override { }

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
  uint64_t size;
  std::unique_ptr<char[]> data;
  std::string error;
  if (!util::readFileContents(args[0], &data, &size, &error)) {
    fprintf(stderr, "error: %s: %s\n", getProgramName(), error.c_str());
    exit(1);
  }

  // Run the parser.
  if (parseOnly) {
      ParseOnlyCommandActions actions;
      ninja::Parser parser(data.get(), size, actions);
      parser.parse();
  } else {
      ParseCommandActions actions(args[0]);
      ninja::Parser parser(data.get(), size, actions);
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

  virtual void error(std::string Filename, std::string Message,
                     const ninja::Token &At) override {
    if (NumErrors++ >= MaxErrors)
      return;

    util::emitError(Filename, Message, At, Loader->getCurrentParser());
  }

  virtual bool readFileContents(const std::string& FromFilename,
                                const std::string& Filename,
                                const ninja::Token* ForToken,
                                std::unique_ptr<char[]> *Data_Out,
                                uint64_t *Length_Out) override {
    // Load the file contents and return if successful.
    std::string Error;
    if (util::readFileContents(Filename, Data_Out, Length_Out, &Error))
      return true;

    // Otherwise, emit the error.
    if (ForToken) {
      util::emitError(FromFilename, Error, *ForToken,
                      Loader->getCurrentParser());
    } else {
      // We were unable to open the main file.
      fprintf(stderr, "error: %s: %s\n", getProgramName(), Error.c_str());
      exit(1);
    }

    return false;
  };

};

}

static int executeLoadManifestCommand(const std::vector<std::string>& args,
                                      bool loadOnly) {
  if (args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getProgramName());
    return 1;
  }

  // Change to the directory containing the input file, so include references
  // can be relative.
  //
  // FIXME: Need llvm::sys::fs.
  std::string filename = args[0];
  size_t pos = filename.find_last_of('/');
  if (pos != std::string::npos) {
    if (!sys::chdir(filename.substr(0, pos).c_str())) {
      fprintf(stderr, "error: %s: unable to chdir(): %s\n",
              getProgramName(), strerror(errno));
      return 1;
    }
    filename = filename.substr(pos+1);
  }

  LoadManifestActions actions;
  ninja::ManifestLoader loader(filename, actions);
  std::unique_ptr<ninja::Manifest> manifest = loader.load();

  // If only loading, we are done.
  if (loadOnly)
    return 0;

  // Dump the manifest.
  std::cout << "# Loaded Manifest: \"" << args[0] << "\"\n";
  std::cout << "\n";

  // Dump the top-level bindings.
  std::cout << "# Top-Level Bindings\n";
  assert(manifest->getBindings().getParentScope() == nullptr);
  std::vector<std::pair<std::string, std::string>> bindings;
  for (const auto& entry: manifest->getBindings().getEntries()) {
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
  for (const auto& entry: manifest->getRules()) {
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
              return a->getOutputs()[0]->getPath() <
                b->getOutputs()[0]->getPath();
            });
  std::cout << "# Commands\n";
  for (const auto& command: commands) {
    // Write the command entry.
    std::cout << "build";
    for (const auto& node: command->getOutputs()) {
      std::cout << " \"" << util::escapedString(node->getPath()) << "\"";
    }
    std::cout << ": " << command->getRule()->getName();
    unsigned count = 0;
    for (const auto& node: command->getInputs()) {
      std::cout << " ";
      if (count == command->getNumExplicitInputs()) {
        std::cout << "| ";
      } else if (count == (command->getNumExplicitInputs() +
                           command->getNumImplicitInputs())) {
        std::cout << "|| ";
      }
      std::cout << "\"" << util::escapedString(node->getPath()) << "\"";
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
        return a->getPath() < b->getPath();
      });
    std::cout << "default ";
    for (const auto& node: defaultTargets) {
      if (node != defaultTargets[0])
        std::cout << " ";
      std::cout << "\"" << node->getPath() << "\"";
    }
    std::cout << "\n\n";
  }

  return 0;
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
