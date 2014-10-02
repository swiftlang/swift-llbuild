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

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace llbuild;

static char hexdigit(unsigned Input) {
  return (Input < 10) ? '0' + Input : 'A' + Input - 10;
}

static std::string escapedString(const char *Start, unsigned Length) {
  std::stringstream Result;
  for (unsigned i = 0; i != Length; ++i) {
    char c = Start[i];
    if (isprint(c)) {
      Result << c;
    } else if (c == '\n') {
      Result << "\\n";
    } else {
      Result << "\\x"
             << hexdigit(((unsigned char) c >> 4) & 0xF)
             << hexdigit((unsigned char) c & 0xF);
    }
  }
  return Result.str();
}
static std::string escapedString(const std::string& String) {
  return escapedString(String.data(), String.size());
}

static void usage() {
  fprintf(stderr, "Usage: %s ninja [--help] <command> [<args>]\n",
          getprogname());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  lex           -- Run the Ninja lexer\n");
  fprintf(stderr, "  parse         -- Run the Ninja parser\n");
  fprintf(stderr, "  load-manifest -- Load a Ninja manifest file\n");
  fprintf(stderr, "\n");
  exit(1);
}

static std::unique_ptr<char[]> ReadFileContents(std::string Path,
                                                uint64_t* Size_Out) {

  // Open the input buffer and compute its size.
  FILE* fp = fopen(Path.c_str(), "rb");
  if (!fp) {
    fprintf(stderr, "error: %s: unable to open input: %s\n", getprogname(),
            Path.c_str());
    exit(1);
  }

  fseek(fp, 0, SEEK_END);
  uint64_t Size = *Size_Out = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // Read the file contents.
  std::unique_ptr<char[]> Data(new char[Size]);
  uint64_t Pos = 0;
  while (Pos < Size) {
    // Read data into the buffer.
    size_t Result = fread(Data.get() + Pos, 1, Size - Pos, fp);
    if (Result <= 0) {
      fprintf(stderr, "error: %s: unable to read input: %s\n", getprogname(),
              Path.c_str());
      exit(1);
    }

    Pos += Result;
  }

  return Data;
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
  std::unique_ptr<char[]> Data = ReadFileContents(Args[0], &Size);

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
                  << " String:\"" << escapedString(Tok.Start,
                                                   Tok.Length) << "\""
                  << " Length:" << Tok.Length
                  << " Line:" << Tok.Line
                  << " Column:" << Tok.Column << ")\n";
    }
  } while (Tok.TokenKind != ninja::Token::Kind::EndOfFile);

  return 0;
}

#pragma mark - Command Utilities

static void emitError(const std::string& Filename, const std::string& Message,
                      const ninja::Token& At, const ninja::Parser* Parser) {
  std::cerr << Filename << ":" << At.Line << ":" << At.Column
            << ": error: " << Message << "\n";

  // Skip carat diagnostics on EOF token.
  if (At.TokenKind == ninja::Token::Kind::EndOfFile)
    return;

  // Simple caret style diagnostics.
  const char *LineBegin = At.Start, *LineEnd = At.Start,
    *BufferBegin = Parser->getLexer().getBufferStart(),
    *BufferEnd = Parser->getLexer().getBufferEnd();

  // Run line pointers forward and back.
  while (LineBegin > BufferBegin &&
         LineBegin[-1] != '\r' && LineBegin[-1] != '\n')
    --LineBegin;
  while (LineEnd < BufferEnd &&
         LineEnd[0] != '\r' && LineEnd[0] != '\n')
    ++LineEnd;

  // Show the line, indented by 2.
  std::cerr << "  " << std::string(LineBegin, LineEnd) << "\n";

  // Show the caret or squiggly, making sure to print back spaces the
  // same.
  std::cerr << "  ";
  for (const char* S = LineBegin; S != At.Start; ++S)
    std::cerr << (isspace(*S) ? *S : ' ');
  if (At.Length > 1) {
    for (unsigned i = 0; i != At.Length; ++i)
      std::cerr << '~';
  } else {
    std::cerr << '^';
  }
  std::cerr << '\n';
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
  virtual void initialize(ninja::Parser *Parser) {
    this->Parser = Parser;
  }

  virtual void error(std::string Message, const ninja::Token &At) override {
    if (NumErrors++ >= MaxErrors)
      return;

    emitError(Filename, Message, At, Parser);
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
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
                                                 Value.Length) << "\")\n";
  }

  virtual void actOnDefaultDecl(const std::vector<ninja::Token>&
                                    Names) override {
    std::cerr << __FUNCTION__ << "(/*Names=*/[";
    bool First = true;
    for (auto& Name: Names) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "])\n";
  }

  virtual void actOnIncludeDecl(bool IsInclude,
                                const ninja::Token& Path) override {
    std::cerr << __FUNCTION__ << "(/*IsInclude=*/"
              << (IsInclude ? "true" : "false") << ", "
              << "/*Path=*/\"" << escapedString(Path.Start, Path.Length)
              << "\")\n";
  }

  virtual BuildResult
  actOnBeginBuildDecl(const ninja::Token& Name,
                      const std::vector<ninja::Token> &Outputs,
                      const std::vector<ninja::Token> &Inputs,
                      unsigned NumExplicitInputs,
                      unsigned NumImplicitInputs) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\""
              << ", /*Outputs=*/[";
    bool First = true;
    for (auto& Name: Outputs) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(Name.Start, Name.Length) << "\"";
      First = false;
    }
    std::cerr << "], /*Inputs=*/[";
    First = true;
    for (auto& Name: Inputs) {
      if (!First)
        std::cerr << ", ";
      std::cerr << "\"" << escapedString(Name.Start, Name.Length) << "\"";
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
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
                                                 Value.Length) << "\")\n";
  }

  virtual void actOnEndBuildDecl(BuildResult Decl,
                                 const ninja::Token& Start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }

  virtual PoolResult actOnBeginPoolDecl(const ninja::Token& Name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\")\n";
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
                                                 Value.Length) << "\")\n";
  }

  virtual void actOnEndPoolDecl(PoolResult Decl,
                                const ninja::Token& Start) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ")\n";
  }

  virtual RuleResult actOnBeginRuleDecl(const ninja::Token& Name) override {
    std::cerr << __FUNCTION__ << "(/*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\")\n";
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult Decl, const ninja::Token& Name,
                                     const ninja::Token& Value) override {
    std::cerr << __FUNCTION__ << "(/*Decl=*/"
              << static_cast<void*>(Decl) << ", /*Name=*/"
              << "\"" << escapedString(Name.Start, Name.Length) << "\", "
              << "/*Value=*/\"" << escapedString(Value.Start,
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
  virtual void initialize(ninja::Parser *Parser) {
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
  std::unique_ptr<char[]> Data = ReadFileContents(Args[0], &Size);

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

private:
  virtual void initialize(ninja::ManifestLoader *Loader) {
    this->Loader = Loader;
  }

  virtual void error(std::string Filename, std::string Message,
                     const ninja::Token &At) override {
    emitError(Filename, Message, At, Loader->getCurrentParser());
  }

  virtual bool readFileContents(std::string Filename,
                                const ninja::Token* ForToken,
                                std::unique_ptr<char[]> *Data_Out,
                                uint64_t *Length_Out) override {
    // FIXME: Error handling.
    *Data_Out = ReadFileContents(Filename, Length_Out);

    return true;
  };

};

}

static int ExecuteLoadManifestCommand(const std::vector<std::string> &Args) {
  if (Args.size() != 1) {
    fprintf(stderr, "error: %s: invalid number of arguments\n",
            getprogname());
    return 1;
  }

  LoadManifestActions Actions;
  ninja::ManifestLoader Loader(Args[0], Actions);
  std::unique_ptr<ninja::Manifest> Manifest = Loader.load();

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
              << escapedString(Entry.second) << "\"\n";
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
                << escapedString(Entry.second) << "\"\n";
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
                                                               Args.end()));
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getprogname(),
            Args[0].c_str());
    return 1;
  }
}
