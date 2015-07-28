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

#include "llbuild/Ninja/Lexer.h"
#include "llbuild/Ninja/Parser.h"

#include <cstdlib>
#include <sstream>
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

    IncludeEntry(const std::string& filename,
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
    auto fileParser = std::make_unique<Parser>(data.get(), length, *this);
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

  std::string evalString(const char* start, const char* end,
                         std::function<std::string(const std::string&)> lookup,
                         std::function<void(const std::string&)> error) {
    // Scan the string for escape sequences or variable references, accumulating
    // output pieces as we go.
    //
    // FIXME: Rewrite this with StringRef once we have it, and make efficient.
    std::stringstream result;
    const char* pos = start;
    while (pos != end) {
      // Find the next '$'.
      const char* pieceStart = pos;
      for (; pos != end; ++pos) {
        if (*pos == '$')
          break;
      }

      // Add the current piece, if non-empty.
      if (pos != pieceStart)
        result << std::string(pieceStart, pos);

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
              result << lookup(std::string(varStart, pos - varStart));
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
        result << lookup(std::string(varStart, pos-varStart));
        continue;
      }

      // Otherwise, we have an invalid '$' escape.
      error("invalid '$'-escape (literal '$' should be written as '$$')");
      break;
    }

    return result.str();
  }

  /// Given a string template token, evaluate it against the given \arg Bindings
  /// and return the resulting string.
  std::string evalString(const Token& value, const BindingSet& bindings) {
    assert(value.tokenKind == Token::Kind::String && "invalid token kind");

    return evalString(value.start, value.start + value.length,
                      /*Lookup=*/ [&](const std::string& name) {
                        return bindings.lookup(name); },
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
    std::string name(nameTok.start, nameTok.length);

    // Evaluate the value string with the current top-level bindings.
    std::string value(evalString(valueTok, getCurrentBindings()));

    getCurrentBindings().insert(name, value);
  }

  virtual void actOnDefaultDecl(const std::vector<Token>& nameToks) override {
    // Resolve all of the inputs and outputs.
    for (const auto& nameTok: nameToks) {
      std::string name(nameTok.start, nameTok.length);

      auto it = theManifest->getNodes().find(name);
      if (it == theManifest->getNodes().end()) {
        error("unknown target name", nameTok);
        continue;
      }

      theManifest->getDefaultTargets().push_back(it->second.get());
    }
  }

  virtual void actOnIncludeDecl(bool isInclude,
                                const Token& pathTok) override {
    std::string path = evalString(pathTok, getCurrentBindings());

    // Enter the new file, with a new binding scope if this is a "subninja"
    // decl.
    if (isInclude) {
      if (enterFile(path, getCurrentBindings(), &pathTok)) {
        // Run the parser for the included file.
        getCurrentParser()->parse();
      }
    } else {
      // Establish a local binding set and use that to contain the bindings for
      // the subninja.
      BindingSet subninjaBindings(&getCurrentBindings());
      if (enterFile(path, subninjaBindings, &pathTok)) {
        // Run the parser for the included file.
        getCurrentParser()->parse();
      }
    }
  }

  virtual BuildResult
  actOnBeginBuildDecl(const Token& nameTok,
                      const std::vector<Token>& outputTokens,
                      const std::vector<Token>& inputTokens,
                      unsigned numExplicitInputs,
                      unsigned numImplicitInputs) override {
    std::string name(nameTok.start, nameTok.length);

    // Resolve the rule.
    auto it = theManifest->getRules().find(name);
    Rule* rule;
    if (it == theManifest->getRules().end()) {
      error("unknown rule", nameTok);

      // Ensure we always have a rule for each command.
      rule = theManifest->getPhonyRule();
    } else {
      rule = it->second.get();
    }

    // Resolve all of the inputs and outputs.
    std::vector<Node*> outputs;
    std::vector<Node*> inputs;
    for (const auto& token: outputTokens) {
      // Evaluate the token string.
      std::string path = evalString(token, getCurrentBindings());
      if (path.empty()) {
        error("empty output path", token);
      }
      outputs.push_back(theManifest->getOrCreateNode(path));
    }
    for (const auto& token: inputTokens) {
      // Evaluate the token string.
      std::string path = evalString(token, getCurrentBindings());
      if (path.empty()) {
        error("empty input path", token);
      }
      inputs.push_back(theManifest->getOrCreateNode(path));
    }

    Command* decl = new Command(rule, outputs, inputs, numExplicitInputs,
                                numImplicitInputs);
    theManifest->getCommands().push_back(std::unique_ptr<Command>(decl));

    return decl;
  }

  virtual void actOnBuildBindingDecl(BuildResult abstractDecl,
                                     const Token& nameTok,
                                     const Token& valueTok) override {
    Command* decl = static_cast<Command*>(abstractDecl);

    std::string name(nameTok.start, nameTok.length);

    // FIXME: It probably should be an error to assign to the same parameter
    // multiple times, but Ninja doesn't diagnose this.

    // The value in a build decl is always evaluated immediately, but only in
    // the context of the top-level bindings.
    decl->getParameters()[name] = evalString(valueTok, getCurrentBindings());
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

    // Create the appropriate binding context.
    //
    // FIXME: Make this efficient.
    std::function<std::string(const std::string&)> lookup;
    lookup = [&](const std::string& name) -> std::string {
      // Support "in" and "out".
      if (name == "in") {
        std::stringstream result;
        for (unsigned i = 0, ie = decl->getNumExplicitInputs(); i != ie; ++i) {
          if (i != 0)
            result << " ";
          result << decl->getInputs()[i]->getPath();
        }
        return result.str();
      } else if (name == "out") {
        std::stringstream result;
        for (unsigned i = 0, ie = decl->getOutputs().size(); i != ie; ++i) {
          if (i != 0)
            result << " ";
          result << decl->getOutputs()[i]->getPath();
        }
        return result.str();
      }

      auto it = decl->getParameters().find(name);
      if (it != decl->getParameters().end())
        return it->second;
      auto it2 = decl->getRule()->getParameters().find(name);
      if (it2 != decl->getRule()->getParameters().end()) {
        const auto& value = it2->second;
        return evalString(value.data(), value.data() + value.size(),
                          /*Lookup=*/ [&](const std::string& name) {
                            // FIXME: Mange recursive lookup? Ninja crashes on
                            // it.
                            return lookup(name); },
                          /*Error=*/ [&](const std::string& msg) {
                            error(msg + " during evaluation of '" + name + "'",
                                  startTok);
                          });
      }
      return getCurrentBindings().lookup(name);
    };

    // Evaluate the build parameters.
    decl->setCommandString(lookup("command"));
    decl->setDescription(lookup("description"));

    // Set the dependency style.
    std::string depsStyleName = lookup("deps");
    std::string depfile = lookup("depfile");
    Command::DepsStyleKind depsStyle = Command::DepsStyleKind::None;
    if (depsStyleName == "") {
      if (!depfile.empty())
        depsStyle = Command::DepsStyleKind::GCC;
    } else if (depsStyleName == "gcc") {
      depsStyle = Command::DepsStyleKind::GCC;
    } else if (depsStyleName == "msvc") {
      depsStyle = Command::DepsStyleKind::MSVC;
    } else {
      error("invalid 'deps' style '" + depsStyleName + "'", startTok);
    }
    decl->setDepsStyle(depsStyle);

    if (!depfile.empty()) {
      if (depsStyle != Command::DepsStyleKind::GCC) {
        error("invalid 'depfile' attribute with selected 'deps' style",
              startTok);
      } else {
        decl->setDepsFile(depfile);
      }
    } else {
      if (depsStyle == Command::DepsStyleKind::GCC) {
        error("missing 'depfile' attribute with selected 'deps' style",
              startTok);
      }
    }

    std::string poolName = lookup("pool");
    if (!poolName.empty()) {
      const auto& it = theManifest->getPools().find(poolName);
      if (it == theManifest->getPools().end()) {
        error("unknown pool '" + poolName + "'", startTok);
      } else {
        decl->setExecutionPool(it->second.get());
      }
    }

    std::string generator = lookup("generator");
    decl->setGeneratorFlag(!generator.empty());

    std::string restat = lookup("restat");
    decl->setRestatFlag(!restat.empty());

    // FIXME: Handle rspfile attributes.
  }

  virtual PoolResult actOnBeginPoolDecl(const Token& nameTok) override {
    std::string name(nameTok.start, nameTok.length);

    // Find the hash slot.
    auto& result = theManifest->getPools()[name];

    // Diagnose if the pool already exists (we still create a new one).
    if (result.get()) {
      // The pool already exists.
      error("duplicate pool", nameTok);
    }

    // Insert the new pool.
    Pool* decl = new Pool(name);
    result.reset(decl);
    return static_cast<PoolResult>(decl);
  }

  virtual void actOnPoolBindingDecl(PoolResult abstractDecl,
                                    const Token& nameTok,
                                    const Token& valueTok) override {
    Pool* decl = static_cast<Pool*>(abstractDecl);

    std::string name(nameTok.start, nameTok.length);

    // Evaluate the value string with the current top-level bindings.
    std::string value(evalString(valueTok, getCurrentBindings()));

    if (name == "depth") {
      const char* start = value.c_str();
      char* end;
      long intValue = ::strtol(start, &end, 10);
      if (*end != '\0' || intValue <= 0) {
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
    std::string name(nameTok.start, nameTok.length);

    // Find the hash slot.
    auto& result = theManifest->getRules()[name];

    // Diagnose if the rule already exists (we still create a new one).
    if (result.get()) {
      // The rule already exists.
      error("duplicate rule", nameTok);
    }

    // Insert the new rule.
    Rule* decl = new Rule(name);
    result.reset(decl);
    return static_cast<RuleResult>(decl);
  }

  virtual void actOnRuleBindingDecl(RuleResult abstractDecl,
                                    const Token& nameTok,
                                    const Token& valueTok) override {
    Rule* decl = static_cast<Rule*>(abstractDecl);

    std::string name(nameTok.start, nameTok.length);
    // FIXME: It probably should be an error to assign to the same parameter
    // multiple times, but Ninja doesn't diagnose this.
    if (Rule::isValidParameterName(name)) {
      decl->getParameters()[name] = std::string(valueTok.start,
                                                valueTok.length);
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

