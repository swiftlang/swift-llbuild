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

#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/ShellUtility.h"
#include "llbuild/Ninja/Lexer.h"
#include "llbuild/Ninja/Parser.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <vector>

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
  struct IncludeEntry {
    /// The file that is being processed.
    std::string filename;
    /// An owning reference to the data consumed by the parser.
    std::unique_ptr<char[]> data;
    /// The parser for the file.
    std::unique_ptr<Parser> parser;
    /// The active binding set.
    BindingSet& bindings;

    IncludeEntry(StringRef filename,
                 std::unique_ptr<char[]> data,
                 std::unique_ptr<class Parser> parser,
                 BindingSet& bindings)
      : filename(filename), data(std::move(data)), parser(std::move(parser)),
        bindings(bindings) {}
  };

  std::string mainFilename;
  ManifestLoaderActions& actions;
  std::unique_ptr<Manifest> theManifest;
  std::vector<IncludeEntry> includeStack;

  // Cached buffers for temporary expansion of possibly large strings. These are
  // lifted out of the function body to ensure we don't blow up the stack
  // unnecesssarily.
  SmallString<10 * 1024> buildCommand;
  SmallString<10 * 1024> buildDescription;

public:
  ManifestLoaderImpl(std::string mainFilename, ManifestLoaderActions& actions)
    : mainFilename(mainFilename), actions(actions), theManifest(nullptr)
  {
  }

  std::unique_ptr<Manifest> load() {
    // Create the manifest.
    theManifest.reset(new Manifest);

    // Enter the main file.
    if (!enterFile(mainFilename, theManifest->getBindings()))
      return nullptr;

    // Run the parser.
    assert(includeStack.size() == 1);
    getCurrentParser()->parse();
    assert(includeStack.size() == 0);

    return std::move(theManifest);
  }

  bool enterFile(const std::string& filename, BindingSet& bindings,
                 const Token* forToken = nullptr) {
    // Load the file data.
    std::unique_ptr<char[]> data;
    uint64_t length;
    std::string fromFilename = includeStack.empty() ? filename :
      getCurrentFilename();
    if (!actions.readFileContents(fromFilename, filename, forToken, &data,
                                  &length))
      return false;

    // Push a new entry onto the include stack.
    auto fileParser = llvm::make_unique<Parser>(data.get(), length, *this);
    includeStack.push_back(IncludeEntry(filename, std::move(data),
                                        std::move(fileParser),
                                        bindings));

    return true;
  }

  void exitCurrentFile() {
    includeStack.pop_back();
  }

  ManifestLoaderActions& getActions() { return actions; }
  Parser* getCurrentParser() const {
    assert(!includeStack.empty());
    return includeStack.back().parser.get();
  }
  const std::string& getCurrentFilename() const {
    assert(!includeStack.empty());
    return includeStack.back().filename;
  }
  BindingSet& getCurrentBindings() const {
    assert(!includeStack.empty());
    return includeStack.back().bindings;
  }

  void evalString(void* userContext, StringRef string, raw_ostream& result,
                  std::function<void(void*, StringRef, raw_ostream&)> lookup,
                  std::function<void(const std::string&)> error) {
    // Scan the string for escape sequences or variable references, accumulating
    // output pieces as we go.
    const char* pos = string.begin();
    const char* end = string.end();
    while (pos != end) {
      // Find the next '$'.
      const char* pieceStart = pos;
      for (; pos != end; ++pos) {
        if (*pos == '$')
          break;
      }

      // Add the current piece, if non-empty.
      if (pos != pieceStart)
        result << StringRef(pieceStart, pos - pieceStart);

      // If we are at the end, we are done.
      if (pos == end)
        break;

      // Otherwise, we have a '$' character to handle.
      ++pos;
      if (pos == end) {
        error("invalid '$'-escape at end of string");
        break;
      }

      // If this is a newline continuation, skip it and all leading space.
      int c = *pos;
      if (c == '\n') {
        ++pos;
        while (pos != end && isspace(*pos))
          ++pos;
        continue;
      }

      // If this is single character escape, honor it.
      if (c == ' ' || c == ':' || c == '$') {
        result << char(c);
        ++pos;
        continue;
      }

      // If this is a braced variable reference, expand it.
      if (c == '{') {
        // Scan until the end of the reference, checking validity of the
        // identifier name as we go.
        ++pos;
        const char* varStart = pos;
        bool isValid = true;
        while (true) {
          // If we reached the end of the string, this is an error.
          if (pos == end) {
            error(
                "invalid variable reference in string (missing trailing '}')");
            break;
          }

          // If we found the end of the reference, resolve it.
          int c = *pos;
          if (c == '}') {
            // If this identifier isn't valid, emit an error.
            if (!isValid) {
              error("invalid variable name in reference");
            } else {
              lookup(userContext, StringRef(varStart, pos - varStart),
                     result);
            }
            ++pos;
            break;
          }

          // Track whether this is a valid identifier.
          if (!Lexer::isIdentifierChar(c))
            isValid = false;

          ++pos;
        }
        continue;
      }

      // If this is a simple variable reference, expand it.
      if (Lexer::isSimpleIdentifierChar(c)) {
        const char* varStart = pos;
        // Scan until the end of the simple identifier.
        ++pos;
        while (pos != end && Lexer::isSimpleIdentifierChar(*pos))
          ++pos;
        lookup(userContext, StringRef(varStart, pos-varStart), result);
        continue;
      }

      // Otherwise, we have an invalid '$' escape.
      error("invalid '$'-escape (literal '$' should be written as '$$')");
      break;
    }
  }

  /// Given a string template token, evaluate it against the given \arg Bindings
  /// and return the resulting string.
  void evalString(const Token& value, const BindingSet& bindings,
                  SmallVectorImpl<char>& storage) {
    assert(value.tokenKind == Token::Kind::String && "invalid token kind");
    
    llvm::raw_svector_ostream result(storage);
    evalString(nullptr, StringRef(value.start, value.length), result,
               /*Lookup=*/ [&](void*, StringRef name, raw_ostream& result) {
                 result << bindings.lookup(name);
               },
               /*Error=*/ [this, &value](const std::string& msg) {
                 error(msg, value);
               });
  }

  /// @name Parse Actions Interfaces
  /// @{

  virtual void initialize(ninja::Parser* parser) override { }

  virtual void error(std::string message, const Token& at) override {
    actions.error(getCurrentFilename(), message, at);
  }

  virtual void actOnBeginManifest(std::string name) override { }

  virtual void actOnEndManifest() override {
    exitCurrentFile();
  }

  virtual void actOnBindingDecl(const Token& nameTok,
                                const Token& valueTok) override {
    // Extract the name string.
    StringRef name(nameTok.start, nameTok.length);

    // Evaluate the value string with the current top-level bindings.
    SmallString<256> value;
    evalString(valueTok, getCurrentBindings(), value);

    getCurrentBindings().insert(name, value.str());
  }

  virtual void actOnDefaultDecl(ArrayRef<Token> nameToks) override {
    // Resolve all of the inputs and outputs.
    for (const auto& nameTok: nameToks) {
      StringRef name(nameTok.start, nameTok.length);

      auto it = theManifest->getNodes().find(name);
      if (it == theManifest->getNodes().end()) {
        error("unknown target name", nameTok);
        continue;
      }

      theManifest->getDefaultTargets().push_back(it->second);
    }
  }

  virtual void actOnIncludeDecl(bool isInclude,
                                const Token& pathTok) override {
    SmallString<256> path;
    evalString(pathTok, getCurrentBindings(), path);

    // Enter the new file, with a new binding scope if this is a "subninja"
    // decl.
    if (isInclude) {
      if (enterFile(path.str(), getCurrentBindings(), &pathTok)) {
        // Run the parser for the included file.
        getCurrentParser()->parse();
      }
    } else {
      // Establish a local binding set and use that to contain the bindings for
      // the subninja.
      BindingSet subninjaBindings(&getCurrentBindings());
      if (enterFile(path.str(), subninjaBindings, &pathTok)) {
        // Run the parser for the included file.
        getCurrentParser()->parse();
      }
    }
  }

  virtual BuildResult
  actOnBeginBuildDecl(const Token& nameTok,
                      ArrayRef<Token> outputTokens,
                      ArrayRef<Token> inputTokens,
                      unsigned numExplicitInputs,
                      unsigned numImplicitInputs) override {
    StringRef name(nameTok.start, nameTok.length);

    // Resolve the rule.
    auto it = theManifest->getRules().find(name);
    Rule* rule;
    if (it == theManifest->getRules().end()) {
      error("unknown rule", nameTok);

      // Ensure we always have a rule for each command.
      rule = theManifest->getPhonyRule();
    } else {
      rule = it->second;
    }

    // Resolve all of the inputs and outputs.
    SmallVector<Node*, 8> outputs;
    SmallVector<Node*, 8> inputs;
    for (const auto& token: outputTokens) {
      // Evaluate the token string.
      SmallString<256> path;
      evalString(token, getCurrentBindings(), path);
      if (path.empty()) {
        error("empty output path", token);
      }
      outputs.push_back(theManifest->getOrCreateNode(path.str()));
    }
    for (const auto& token: inputTokens) {
      // Evaluate the token string.
      SmallString<256> path;
      evalString(token, getCurrentBindings(), path);
      if (path.empty()) {
        error("empty input path", token);
      }
      inputs.push_back(theManifest->getOrCreateNode(path.str()));
    }

    Command* decl = new (theManifest->getAllocator())
      Command(rule, outputs, inputs, numExplicitInputs, numImplicitInputs);
    theManifest->getCommands().push_back(decl);

    return decl;
  }

  virtual void actOnBuildBindingDecl(BuildResult abstractDecl,
                                     const Token& nameTok,
                                     const Token& valueTok) override {
    Command* decl = static_cast<Command*>(abstractDecl);

    StringRef name(nameTok.start, nameTok.length);

    // FIXME: It probably should be an error to assign to the same parameter
    // multiple times, but Ninja doesn't diagnose this.

    // The value in a build decl is always evaluated immediately, but only in
    // the context of the top-level bindings.
    SmallString<256> value;
    evalString(valueTok, getCurrentBindings(), value);
    
    decl->getParameters()[name] = value.str();
  }

  struct LookupContext {
    ManifestLoaderImpl& loader;
    Command* decl;
    const Token& startTok;
    bool shellEscapeInAndOut;
  };
  static void lookupBuildParameter(void* userContext, StringRef name,
                                   raw_ostream& result) {
    LookupContext* context = static_cast<LookupContext*>(userContext);
    context->loader.lookupBuildParameterImpl(context, name, result);
  }
  void lookupBuildParameterImpl(LookupContext* context, StringRef name,
                                raw_ostream& result) {
    auto decl = context->decl;
      
    // FIXME: Mange recursive lookup? Ninja crashes on it.
      
    // Support "in" and "out".
    if (name == "in") {
      for (unsigned i = 0, ie = decl->getNumExplicitInputs(); i != ie; ++i) {
        if (i != 0)
          result << " ";
        auto &path = decl->getInputs()[i]->getPath();
        result << (context->shellEscapeInAndOut ? basic::shellEscaped(path)
                                                : path);
      }
      return;
    } else if (name == "out") {
      for (unsigned i = 0, ie = decl->getOutputs().size(); i != ie; ++i) {
        if (i != 0)
          result << " ";
        auto &path = decl->getOutputs()[i]->getPath();
        result << (context->shellEscapeInAndOut ? basic::shellEscaped(path)
                                                : path);
      }
      return;
    }

    auto it = decl->getParameters().find(name);
    if (it != decl->getParameters().end()) {
      result << it->second;
      return;
    }
    auto it2 = decl->getRule()->getParameters().find(name);
    if (it2 != decl->getRule()->getParameters().end()) {
      evalString(context, it2->second, result, lookupBuildParameter,
                 /*Error=*/ [&](const std::string& msg) {
                   error(msg + " during evaluation of '" + name.str() + "'",
                         context->startTok);
                 });
      return;
    }
      
    result << context->loader.getCurrentBindings().lookup(name);
  }
  StringRef lookupNamedBuildParameter(Command* decl, const Token& startTok,
                                      StringRef name,
                                      SmallVectorImpl<char>& storage) {
    LookupContext context{*this, decl, startTok,
                          /*shellEscapeInAndOut*/ name == "command"};
    llvm::raw_svector_ostream os(storage);
    lookupBuildParameter(&context, name, os);
    return os.str();
  }
  
  virtual void actOnEndBuildDecl(BuildResult abstractDecl,
                                const Token& startTok) override {
    Command* decl = static_cast<Command*>(abstractDecl);

    // Resolve the build decl parameters by evaluating in the context of the
    // rule and parameter overrides.
    //
    // FIXME: Eventually, we should evaluate whether it would be more efficient
    // to lazily bind all of these by only storing the parameters for the
    // commands. This would let us delay the computation of all of the "command"
    // strings until right before the command is run, which would then be
    // parallelized and could also be more memory efficient. However, that would
    // also requires us to expose more of the string evaluation machinery, as
    // well as ensure that the recursive binding sets used by "subninja" decls
    // are properly stored.

    // FIXME: There is no need to store the parameters in the build decl anymore
    // once this is all complete.
    
    // Evaluate the build parameters.
    buildCommand.clear();
    decl->setCommandString(lookupNamedBuildParameter(
                               decl, startTok, "command", buildCommand));
    buildDescription.clear();
    decl->setDescription(lookupNamedBuildParameter(
                             decl, startTok, "description", buildDescription));

    // Set the dependency style.
    SmallString<256> deps;
    lookupNamedBuildParameter(decl, startTok, "deps", deps);
    SmallString<256> depfile;
    lookupNamedBuildParameter(decl, startTok, "depfile", depfile);
    Command::DepsStyleKind depsStyle = Command::DepsStyleKind::None;
    if (deps.str() == "") {
      if (!depfile.empty())
        depsStyle = Command::DepsStyleKind::GCC;
    } else if (deps.str() == "gcc") {
      depsStyle = Command::DepsStyleKind::GCC;
    } else if (deps.str() == "msvc") {
      depsStyle = Command::DepsStyleKind::MSVC;
    } else {
      error("invalid 'deps' style '" + deps.str().str() + "'", startTok);
    }
    decl->setDepsStyle(depsStyle);

    if (!depfile.str().empty()) {
      if (depsStyle != Command::DepsStyleKind::GCC) {
        error("invalid 'depfile' attribute with selected 'deps' style",
              startTok);
      } else {
        decl->setDepsFile(depfile.str());
      }
    } else {
      if (depsStyle == Command::DepsStyleKind::GCC) {
        error("missing 'depfile' attribute with selected 'deps' style",
              startTok);
      }
    }

    SmallString<256> poolName;
    lookupNamedBuildParameter(decl, startTok, "pool", poolName);
    if (!poolName.empty()) {
      const auto& it = theManifest->getPools().find(poolName.str());
      if (it == theManifest->getPools().end()) {
        error("unknown pool '" + poolName.str().str() + "'", startTok);
      } else {
        decl->setExecutionPool(it->second);
      }
    }

    SmallString<256> generator;
    lookupNamedBuildParameter(decl, startTok, "generator", generator);
    decl->setGeneratorFlag(!generator.str().empty());

    SmallString<256> restat;
    lookupNamedBuildParameter(decl, startTok, "restat", restat);
    decl->setRestatFlag(!restat.str().empty());

    // FIXME: Handle rspfile attributes.
  }

  virtual PoolResult actOnBeginPoolDecl(const Token& nameTok) override {
    StringRef name(nameTok.start, nameTok.length);

    // Find the hash slot.
    auto& result = theManifest->getPools()[name];

    // Diagnose if the pool already exists (we still create a new one).
    if (result) {
      // The pool already exists.
      error("duplicate pool", nameTok);
    }

    // Insert the new pool.
    Pool* decl = new (theManifest->getAllocator()) Pool(name);
    result = decl;
    return static_cast<PoolResult>(decl);
  }

  virtual void actOnPoolBindingDecl(PoolResult abstractDecl,
                                    const Token& nameTok,
                                    const Token& valueTok) override {
    Pool* decl = static_cast<Pool*>(abstractDecl);

    StringRef name(nameTok.start, nameTok.length);

    // Evaluate the value string with the current top-level bindings.
    SmallString<256> value;
    evalString(valueTok, getCurrentBindings(), value);

    if (name == "depth") {
      long intValue;
      if (value.str().getAsInteger(10, intValue) || intValue <= 0) {
        error("invalid depth", valueTok);
      } else {
        decl->setDepth(static_cast<uint32_t>(intValue));
      }
    } else {
      error("unexpected variable", nameTok);
    }
  }

  virtual void actOnEndPoolDecl(PoolResult abstractDecl,
                                const Token& startTok) override {
    Pool* decl = static_cast<Pool*>(abstractDecl);

    // It is an error to not specify the pool depth.
    if (decl->getDepth() == 0) {
      error("missing 'depth' variable assignment", startTok);
    }
  }

  virtual RuleResult actOnBeginRuleDecl(const Token& nameTok) override {
    StringRef name(nameTok.start, nameTok.length);

    // Find the hash slot.
    auto& result = theManifest->getRules()[name];

    // Diagnose if the rule already exists (we still create a new one).
    if (result) {
      // The rule already exists.
      error("duplicate rule", nameTok);
    }

    // Insert the new rule.
    Rule* decl = new (theManifest->getAllocator()) Rule(name);
    result = decl;
    return static_cast<RuleResult>(decl);
  }

  virtual void actOnRuleBindingDecl(RuleResult abstractDecl,
                                    const Token& nameTok,
                                    const Token& valueTok) override {
    Rule* decl = static_cast<Rule*>(abstractDecl);

    StringRef name(nameTok.start, nameTok.length);
    // FIXME: It probably should be an error to assign to the same parameter
    // multiple times, but Ninja doesn't diagnose this.
    if (Rule::isValidParameterName(name)) {
      decl->getParameters()[name] = StringRef(valueTok.start, valueTok.length);
    } else {
      error("unexpected variable", nameTok);
    }
  }

  virtual void actOnEndRuleDecl(RuleResult abstractDecl,
                                const Token& startTok) override {
    Rule* decl = static_cast<Rule*>(abstractDecl);

    if (!decl->getParameters().count("command")) {
      error("missing 'command' variable assignment", startTok);
    }
  }

  /// @}
};

}

#pragma mark - ManifestLoader

ManifestLoader::ManifestLoader(std::string filename,
                               ManifestLoaderActions &actions)
  : impl(static_cast<void*>(new ManifestLoaderImpl(filename, actions)))
{
}

ManifestLoader::~ManifestLoader() {
  delete static_cast<ManifestLoaderImpl*>(impl);
}

std::unique_ptr<Manifest> ManifestLoader::load() {
  // Initialize the actions.
  static_cast<ManifestLoaderImpl*>(impl)->getActions().initialize(this);

  return static_cast<ManifestLoaderImpl*>(impl)->load();
}

const Parser* ManifestLoader::getCurrentParser() const {
  return static_cast<const ManifestLoaderImpl*>(impl)->getCurrentParser();
}
