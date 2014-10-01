//===-- ManifestLoader.cpp ------------------------------------------------===//
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

#include "llbuild/Ninja/ManifestLoader.h"

#include "llbuild/Ninja/Parser.h"

using namespace llbuild;
using namespace llbuild::ninja;

#pragma mark - ManifestLoaderActions

ManifestLoaderActions::~ManifestLoaderActions() {
}

#pragma mark - ManifestLoader Implementation

namespace {

/// Manifest loader implementation.
///
/// For simplicity, we just directly implement the parser actions interface.
class ManifestLoaderImpl: public ParseActions {
  std::string MainFilename;
  ManifestLoaderActions &Actions;
  std::unique_ptr<Manifest> TheManifest;
  std::unique_ptr<Parser> CurrentParser;

public:
  ManifestLoaderImpl(std::string MainFilename, ManifestLoaderActions &Actions)
    : MainFilename(MainFilename), Actions(Actions), TheManifest(nullptr),
      CurrentParser(nullptr)
  {
  }

  std::unique_ptr<Manifest> load() {
    // Create the manifest.
    TheManifest.reset(new Manifest);

    // Load the main file data.
    std::unique_ptr<char[]> Data;
    uint64_t Length;
    if (!Actions.readFileContents(MainFilename, nullptr, &Data, &Length))
      return nullptr;

    // Create the parser for the data.
    CurrentParser.reset(new Parser(Data.get(), Length, *this));
    CurrentParser->parse();

    return std::move(TheManifest);
  }

  /// Given a string template token, evaluate it against the given \arg Bindings
  /// and return the resulting string.
  std::string evalString(const Token& Value, const BindingSet& Bindings) {
    assert(Value.TokenKind == Token::Kind::String && "invalid token kind");

    // FIXME: Implement binding resolution.

    return std::string(Value.Start, Value.Length);
  }

  /// @name Parse Actions Interfaces
  /// @{

  virtual void initialize(ninja::Parser *Parser) override { }

  virtual void error(std::string Message, const Token &At) override { }

  virtual void actOnBeginManifest(std::string Name) override { }

  virtual void actOnEndManifest() override { }

  virtual void actOnBindingDecl(const Token& NameTok,
                                const Token& ValueTok) override {
    // Extract the name string.
    std::string Name(NameTok.Start, NameTok.Length);

    // Evaluate the value string with the current top-level bindings.
    std::string Value(evalString(ValueTok, TheManifest->Bindings));

    TheManifest->Bindings.Bindings.insert(std::make_pair(Name, Value));
  }

  virtual void actOnDefaultDecl(const std::vector<Token>& Names) override { }

  virtual void actOnIncludeDecl(bool IsInclude,
                                const Token& Path) override { }

  virtual BuildResult
  actOnBeginBuildDecl(const Token& Name,
                      const std::vector<Token> &Outputs,
                      const std::vector<Token> &Inputs,
                      unsigned NumExplicitInputs,
                      unsigned NumImplicitInputs) override {
    return 0;
  }

  virtual void actOnBuildBindingDecl(BuildResult Decl, const Token& Name,
                                     const Token& Value) override { }

  virtual void actOnEndBuildDecl(PoolResult Decl) override { }

  virtual PoolResult actOnBeginPoolDecl(const Token& Name) override {
    return 0;
  }

  virtual void actOnPoolBindingDecl(PoolResult Decl, const Token& Name,
                                    const Token& Value) override { }

  virtual void actOnEndPoolDecl(PoolResult Decl) override { }

  virtual RuleResult actOnBeginRuleDecl(const Token& Name) override {
    return 0;
  }

  virtual void actOnRuleBindingDecl(RuleResult Decl, const Token& Name,
                                    const Token& Value) override { }

  virtual void actOnEndRuleDecl(PoolResult Decl) override { }

  /// @}
};

}

#pragma mark - ManifestLoader

ManifestLoader::ManifestLoader(std::string Filename,
                               ManifestLoaderActions &Actions)
  : Impl(static_cast<void*>(new ManifestLoaderImpl(Filename, Actions)))
{
}

ManifestLoader::~ManifestLoader() {
  delete static_cast<ManifestLoaderImpl*>(Impl);
}

std::unique_ptr<Manifest> ManifestLoader::load() {
  return static_cast<ManifestLoaderImpl*>(Impl)->load();
}
