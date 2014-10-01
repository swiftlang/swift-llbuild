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

#include <sstream>

using namespace llbuild;
using namespace llbuild::ninja;

#pragma mark - ManifestLoaderActions

ManifestLoaderActions::~ManifestLoaderActions() {
}

#pragma mark - ManifestLoader Implementation

namespace {

/// Check if the given \arg Char is valid as a "simple" (non braced) variable
/// identifier name.
static bool isSimpleIdentifierChar(int Char) {
  return (Char >= 'a' && Char <= 'z') ||
    (Char >= 'A' && Char <= 'Z') ||
    (Char >= '0' && Char <= '9') ||
    Char == '_' || Char == '-';
}

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

  ManifestLoaderActions& getActions() { return Actions; }
  const Parser* getCurrentParser() const { return CurrentParser.get(); }

  /// Given a string template token, evaluate it against the given \arg Bindings
  /// and return the resulting string.
  std::string evalString(const Token& Value, const BindingSet& Bindings) {
    assert(Value.TokenKind == Token::Kind::String && "invalid token kind");

    // Scan the string for escape sequences or variable references, accumulating
    // output pieces as we go.
    //
    // FIXME: Rewrite this with StringRef once we have it, and make efficient.
    std::stringstream Result;
    const char* Start = Value.Start;
    const char* End = Value.Start + Value.Length;
    const char* Pos = Start;
    while (Pos != End) {
      // Find the next '$'.
      const char* PieceStart = Pos;
      for (; Pos != End; ++Pos) {
        if (*Pos == '$')
          break;
      }

      // Add the current piece, if non-empty.
      if (Pos != PieceStart)
        Result << std::string(PieceStart, Pos);

      // If we are at the end, we are done.
      if (Pos == End)
        break;

      // Otherwise, we have a '$' character to handle.
      ++Pos;
      if (Pos == End) {
        error("invalid '$'-escape at end of string", Value);
        break;
      }

      // If this is a newline continuation, skip it and all leading space.
      char Char = *Pos;
      if (Char == '\n') {
        ++Pos;
        while (Pos != End && isspace(*Pos))
          ++Pos;
        continue;
      }

      // If this is single character escape, honor it.
      if (Char == ' ' || Char == ':' || Char == '$') {
        Result << Char;
        ++Pos;
        continue;
      }

      // If this is a braced variable reference, expand it.
      if (Char == '{') {
        ++Pos;
        const char* VarStart = Pos;
        bool IsValid = true;
        while (true) {
          // If we reached the end of the string, this is an error.
          if (Pos == End) {
            error("invalid variable reference in string (missing trailing '}')",
                  Value);
            break;
          }

          // If we found the end of the reference, resolve it.
          if (*Pos == '}') {
            // FIXME: We also should check that this is a valid identifier
            // name here.
            Result << Bindings.lookup(std::string(VarStart, Pos - VarStart));
            ++Pos;
            break;
          }

          ++Pos;
        }
        continue;
      }

      // If this is a simple variable reference, expand it.
      if (isSimpleIdentifierChar(Char)) {
        const char* VarStart = Pos;
        // Scan until the end of the simple identifier.
        ++Pos;
        while (Pos != End && isSimpleIdentifierChar(*Pos))
          ++Pos;
        Result << Bindings.lookup(std::string(VarStart, Pos-VarStart));
        continue;
      }

      // Otherwise, we have an invalid '$' escape.
      error("invalid '$'-escape (literal '$' should be written as '$$')",
            Value);
      break;
    }

    return Result.str();
  }

  /// @name Parse Actions Interfaces
  /// @{

  virtual void initialize(ninja::Parser *Parser) override { }

  virtual void error(std::string Message, const Token &At) override {
    Actions.error(MainFilename, Message, At);
  }

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
  // Initialize the actions.
  static_cast<ManifestLoaderImpl*>(Impl)->getActions().initialize(this);

  return static_cast<ManifestLoaderImpl*>(Impl)->load();
}

const Parser* ManifestLoader::getCurrentParser() const {
  return static_cast<const ManifestLoaderImpl*>(Impl)->getCurrentParser();
}

