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

#include <unistd.h>

using namespace llbuild;
using namespace llbuild::commands;

static void usage() {
  fprintf(stderr, "Usage: %s ninja [--help] <command> [<args>]\n",
          getprogname());
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

static int ExecuteLexCommand(const std::vector<std::string> &Args,
                             bool LexOnly) {

  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  // Read the input.
  uint64_t Size;
  std::unique_ptr<char[]> Data;
  std::string Error;
  if (!util::ReadFileContents(Args[0], &Data, &Size, &Error)) {
    fprintf(stderr, "error: %s: %s\n", getprogname(), Error.c_str());
    exit(1);
  }

  // Create a Ninja lexer.
  if (!LexOnly) {
      fprintf(stderr, "note: %s: reading tokens from %s\n", getprogname(),
              Args[0].c_str());
  }
  ninja::Lexer Lexer(Data.get(), Size);
  ninja::Token Tok;

  do {
    // Get the next token.
    Lexer.lex(Tok);

    if (!LexOnly) {
        std::cerr << "(Token \"" << Tok.getKindName() << "\""
                  << " String:\"" << util::EscapedString(Tok.Start,
                                                         Tok.Length) << "\""
                  << " Length:" << Tok.Length
                  << " Line:" << Tok.Line
                  << " Column:" << Tok.Column << ")\n";
    }
  } while (Tok.TokenKind != ninja::Token::Kind::EndOfFile);

  return 0;
}

#pragma mark - Parse Command

namespace {

class ParseCommandActions : public ninja::ParseActions {
  std::string Filename;
  unsigned NumErrors = 0;
  unsigned MaxErrors = 20;
  ninja::Parser *Parser = 0;

public:
  ParseCommandActions(std::string Filename) : Filename(Filename) {}

private:
  virtual void initialize(ninja::Parser *Parser) override {
    this->Parser = Parser;
  }

  virtual void error(std::string Message, const ninja::Token &At) override {
    if (NumErrors++ >= MaxErrors) {
      return;
    }

    util::EmitError(Filename, Message, At, Parser);
  }

  virtual void actOnBeginManifest(std::string Name) override {
    std::cerr << __FUNCTION__ << "(\"" << Name << "\")\n";
  }

  virtual void actOnEndManifest() override {
    std::cerr << __FUNCTION__ << "()\n";
  }

  virtual void actOnBindingDecl(const ninja::Token& Name,
                                const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::EscapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << util::EscapedString(Value.Start,
                                                       Value.Length) << "\")\n";
  }

  virtual void actOnDefaultDecl(const std::vector<ninja::Token>&
                                    Names) override {
    std::cerr << __FUNCTION__ << "(/*Names=*/[";
    bool First = true;
    for (auto& Name: Names) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << util::EscapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "])\n";
  }

  virtual void actOnIncludeDecl(bool IsInclude,
                                const ninja::Token& Path) override {
    std::cerr << __FUNCTION__ << "(/*IsInclude=*/"
              << (IsInclude ? "true" : "false") << ", "
              << "/*Path=*/\"" << util::EscapedString(Path.Start, Path.Length)
              << "\")\n";
  }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& Name,
                      const std::vector<ninja::Token> &Outputs,
                      const std::vector<ninja::Token> &Inputs,
                      unsigned NumExplicitInputs,
                      unsigned NumImplicitInputs) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::EscapedString(Name.Start, Name.Length) << "\""
              << ", /*Outputs=*/[";
    bool First = true;
    for (auto& Name: Outputs) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << util::EscapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "], /*Inputs=*/[";
    First = true;
    for (auto& Name: Inputs) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << util::EscapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "], /*NumExplicitInputs=*/" << NumExplicitInputs
              << ", /*NumImplicitInputs=*/"  << NumImplicitInputs << ")\n";
    return 0;
  }

  virtual void actOnBuildBindingDecl(BuildResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << util::EscapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << util::EscapedString(Value.Start,
                                                       Value.Length) << "\")\n";
  }

  virtual void actOnEndBuildDecl(BuildResult Decl,
                                 const ninja::Token& Start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& Name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::EscapedString(Name.Start,
                                             Name.Length) << "\")\n";
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << util::EscapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << util::EscapedString(Value.Start,
                                                       Value.Length) << "\")\n";
  }

  virtual void actOnEndPoolDecl(PoolResult Decl,
                                const ninja::Token& Start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& Name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << util::EscapedString(Name.Start,
                                             Name.Length) << "\")\n";
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << util::EscapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << util::EscapedString(Value.Start,
                                                       Value.Length) << "\")\n";
  }

  virtual void actOnEndRuleDecl(RuleResult Decl,
                                const ninja::Token& Start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }
};

}

#pragma mark - Parse Only Command

namespace {

class ParseOnlyCommandActions : public ninja::ParseActions {
  ninja::Parser *Parser = 0;

public:
  ParseOnlyCommandActions() {}

private:
  virtual void initialize(ninja::Parser *Parser) override {
    this->Parser = Parser;
  }

  virtual void error(std::string Message, const ninja::Token &At) override { }

  virtual void actOnBeginManifest(std::string Name) override { }

  virtual void actOnEndManifest() override { }

  virtual void actOnBindingDecl(const ninja::Token& Name,
                                const ninja::Token& Value) override { }

  virtual void actOnDefaultDecl(const std::vector<ninja::Token>&
                                    Names) override { }

  virtual void actOnIncludeDecl(bool IsInclude,
                                const ninja::Token& Path) override { }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& Name,
                      const std::vector<ninja::Token> &Outputs,
                      const std::vector<ninja::Token> &Inputs,
                      unsigned NumExplicitInputs,
                      unsigned NumImplicitInputs) override {
    return 0;
  }

  virtual void actOnBuildBindingDecl(BuildResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override { }

  virtual void actOnEndBuildDecl(BuildResult Decl,
                                 const ninja::Token& Start) override { }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& Name) override {
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult Decl, const ninja::Token& Name,
                                    const ninja::Token& Value) override { }

  virtual void actOnEndPoolDecl(PoolResult Decl,
                                const ninja::Token& Start) override { }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& Name) override {
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult Decl, const ninja::Token& Name,
                                    const ninja::Token& Value) override { }

  virtual void actOnEndRuleDecl(RuleResult Decl,
                                 const ninja::Token& Start) override { }
};

}

static int ExecuteParseCommand(const std::vector<std::string> &Args,
                               bool ParseOnly) {
  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  // Read the input.
  uint64_t Size;
  std::unique_ptr<char[]> Data;
  std::string Error;
  if (!util::ReadFileContents(Args[0], &Data, &Size, &Error)) {
    fprintf(stderr, "error: %s: %s\n", getprogname(), Error.c_str());
    exit(1);
  }

  // Run the parser.
  if (ParseOnly) {
      ParseOnlyCommandActions Actions;
      ninja::Parser Parser(Data.get(), Size, Actions);
      Parser.parse();
  } else {
      ParseCommandActions Actions(Args[0]);
      ninja::Parser Parser(Data.get(), Size, Actions);
      Parser.parse();
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

    util::EmitError(Filename, Message, At, Loader->getCurrentParser());
  }

  virtual bool readFileContents(const std::string& FromFilename,
                                const std::string& Filename,
                                const ninja::Token* ForToken,
                                std::unique_ptr<char[]> *Data_Out,
                                uint64_t *Length_Out) override {
    // Load the file contents and return if successful.
    std::string Error;
    if (util::ReadFileContents(Filename, Data_Out, Length_Out, &Error))
      return true;

    // Otherwise, emit the error.
    if (ForToken) {
      util::EmitError(FromFilename, Error, *ForToken,
                      Loader->getCurrentParser());
    } else {
      // We were unable to open the main file.
      fprintf(stderr, "error: %s: %s\n", getprogname(), Error.c_str());
      exit(1);
    }

    return false;
  };

};

}

static int ExecuteLoadManifestCommand(const std::vector<std::string> &Args,
                                      bool LoadOnly) {
  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  // Change to the directory containing the input file, so include references
  // can be relative.
  //
  // FIXME: Need llvm::sys::fs.
  std::string Filename = Args[0];
  size_t Pos = Filename.find_last_of('/');
  if (Pos != std::string::npos) {
    if (::chdir(std::string(Filename.substr(0, Pos)).c_str()) < 0) {
      fprintf(stderr, "error: %s: unable to chdir(): %s\n",
              getprogname(), strerror(errno));
      return 1;
    }
    Filename = Filename.substr(Pos+1);
  }

  LoadManifestActions Actions;
  ninja::ManifestLoader Loader(Filename, Actions);
  std::unique_ptr<ninja::Manifest> Manifest = Loader.load();

  // If only loading, we are done.
  if (LoadOnly)
    return 0;

  // Dump the manifest.
  std::cout << "# Loaded Manifest: \"" << Args[0] << "\"\n";
  std::cout << "\n";

  // Dump the top-level bindings.
  std::cout << "# Top-Level Bindings\n";
  assert(Manifest->getBindings().getParentScope() == nullptr);
  std::vector<std::pair<std::string, std::string>>
    Bindings(Manifest->getBindings().getEntries().begin(),
             Manifest->getBindings().getEntries().end());
  std::sort(Bindings.begin(), Bindings.end());
  for (auto& Entry: Bindings) {
    std::cout << Entry.first << " = \""
              << util::EscapedString(Entry.second) << "\"\n";
  }
  std::cout << "\n";

  // Dump the pools, if present.
  if (!Manifest->getPools().empty()) {
    std::cout << "# Pools\n";
    std::vector<ninja::Pool*> Pools;
    for (auto& Entry: Manifest->getPools()) {
      Pools.push_back(Entry.second.get());
    }
    std::sort(Pools.begin(), Pools.end(), [] (ninja::Pool* a, ninja::Pool* b) {
        return a->getName() < b->getName();
      });
    for (auto Pool: Pools) {
      // Write the rule entry.
      std::cout << "pool " << Pool->getName() << "\n";
      if (uint32_t Depth = Pool->getDepth()) {
        std::cout << "  depth = " << Depth << "\n";
      }
      std::cout << "\n";
    }
  }

  // Dump the rules.
  std::cout << "# Rules\n";
  std::vector<ninja::Rule*> Rules;
  for (auto& Entry: Manifest->getRules()) {
    Rules.push_back(Entry.second.get());
  }
  std::sort(Rules.begin(), Rules.end(), [] (ninja::Rule* a, ninja::Rule* b) {
      return a->getName() < b->getName();
    });
  for (auto Rule: Rules) {
    // Write the rule entry.
    std::cout << "rule " << Rule->getName() << "\n";

    // Write the parameters.
    std::vector<std::pair<std::string, std::string>>
      Parameters(Rule->getParameters().begin(), Rule->getParameters().end());
    std::sort(Parameters.begin(), Parameters.end());
    for (auto& Entry: Parameters) {
      std::cout << "  " << Entry.first << " = \""
                << util::EscapedString(Entry.second) << "\"\n";
    }
    std::cout << "\n";
  }

  // Dump the commands.
  std::vector<ninja::Command*> Commands;
  for (auto& Entry: Manifest->getCommands()) {
    Commands.push_back(Entry.get());
  }
  std::sort(Commands.begin(), Commands.end(),
            [] (ninja::Command* a, ninja::Command* b) {
              // Commands can not have duplicate outputs, so comparing based
              // only on the first still provides a total ordering.
              return a->getOutputs()[0]->getPath() <
                b->getOutputs()[0]->getPath();
            });
  std::cout << "# Commands\n";
  for (auto Command: Commands) {
    // Write the command entry.
    std::cout << "build";
    for (auto Node: Command->getOutputs()) {
      std::cout << " \"" << util::EscapedString(Node->getPath()) << "\"";
    }
    std::cout << ": " << Command->getRule()->getName();
    unsigned Count = 0;
    for (auto Node: Command->getInputs()) {
      std::cout << " ";
      if (Count == Command->getNumExplicitInputs()) {
        std::cout << "| ";
      } else if (Count == (Command->getNumExplicitInputs() +
                           Command->getNumImplicitInputs())) {
        std::cout << "|| ";
      }
      std::cout << "\"" << util::EscapedString(Node->getPath()) << "\"";
      ++Count;
    }
    std::cout << "\n";

    // Write out the attributes.
    std::cout << "  command = \""
              << util::EscapedString(Command->getCommandString()) << "\"\n";
    std::cout << "  description = \""
              << util::EscapedString(Command->getDescription()) << "\"\n";

    switch (Command->getDepsStyle()) {
    case ninja::Command::DepsStyleKind::None:
      break;
    case ninja::Command::DepsStyleKind::GCC:
      std::cout << "  deps = gcc\n";
      std::cout << "  depfile = \""
                << util::EscapedString(Command->getDepsFile()) << "\"\n";
      break;
    case ninja::Command::DepsStyleKind::MSVC:
      std::cout << "  deps = msvc\n";
      break;
    }
  
    if (Command->hasGeneratorFlag())
      std::cout << "  generator = 1\n";
    if (Command->hasRestatFlag())
      std::cout << "  restat = 1\n";

    if (ninja::Pool* ExecutionPool = Command->getExecutionPool()) {
      std::cout << "  pool = " << ExecutionPool->getName() << "\n";
    }

    std::cout << "\n";
  }

  // Dump the default targets, if specified.
  if (!Manifest->getDefaultTargets().empty()) {
    std::cout << "# Default Targets\n";
    std::vector<ninja::Node*> DefaultTargets = Manifest->getDefaultTargets();
    std::sort(DefaultTargets.begin(), DefaultTargets.end(),
              [] (ninja::Node* a, ninja::Node* b) {
        return a->getPath() < b->getPath();
      });
    std::cout << "default ";
    for (auto Node: DefaultTargets) {
      if (Node != DefaultTargets[0])
        std::cout << " ";
      std::cout << "\"" << Node->getPath() << "\"";
    }
    std::cout << "\n\n";
  }

  return 0;
}

#pragma mark - Ninja Top-Level Command

int commands::ExecuteNinjaCommand(const std::vector<std::string> &Args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (Args.empty() || Args[0] == "--help")
    usage();

  if (Args[0] == "lex") {
    return ExecuteLexCommand(std::vector<std::string>(Args.begin()+1,
                                                      Args.end()),
                             /*LexOnly=*/false);
  } else if (Args[0] == "lex-only") {
    return ExecuteLexCommand(std::vector<std::string>(Args.begin()+1,
                                                      Args.end()),
                             /*LexOnly=*/true);
  } else if (Args[0] == "parse") {
    return ExecuteParseCommand(std::vector<std::string>(Args.begin()+1,
                                                        Args.end()),
                               /*ParseOnly=*/false);
  } else if (Args[0] == "parse-only") {
    return ExecuteParseCommand(std::vector<std::string>(Args.begin()+1,
                                                        Args.end()),
                               /*ParseOnly=*/true);
  } else if (Args[0] == "load-manifest") {
    return ExecuteLoadManifestCommand(std::vector<std::string>(Args.begin()+1,
                                                               Args.end()),
                                      /*LoadOnly=*/false);
  } else if (Args[0] == "load-manifest-only") {
    return ExecuteLoadManifestCommand(std::vector<std::string>(Args.begin()+1,
                                                               Args.end()),
                                      /*LoadOnly=*/true);
  } else if (Args[0] == "build") {
    return ExecuteNinjaBuildCommand(std::vector<std::string>(Args.begin()+1,
                                                             Args.end()));
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getprogname(),
            Args[0].c_str());
    return 1;
  }
}
