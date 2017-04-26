//===-- SwiftTools.cpp ----------------------------------------------------===//
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

#include "llbuild/BuildSystem/CommandResult.h"
#include "llbuild/BuildSystem/SwiftTools.h"

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/Hashing.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildValue.h"
#include "llbuild/BuildSystem/BuildSystemCommandInterface.h"
#include "llbuild/BuildSystem/ExternalCommand.h"
#include "llbuild/Core/MakefileDepsParser.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llbuild;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

namespace {

class SwiftGetVersionCommand : public Command {
  std::string executable;
  
public:
  SwiftGetVersionCommand(const BuildKey& key)
      : Command("swift-get-version"), executable(key.getCustomTaskData()) {
  }

  // FIXME: Should create a CustomCommand class, to avoid all the boilerplate
  // required implementations.

  virtual void getShortDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << "Checking Swift Compiler Version";
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
    llvm::raw_svector_ostream(result) << '"' << executable << '"'
                                      << " --version";
  }

  virtual void configureDescription(const ConfigureContext&,
                                    StringRef value) override { }
  virtual void configureInputs(const ConfigureContext&,
                               const std::vector<Node*>& value) override { }
  virtual void configureOutputs(const ConfigureContext&,
                                const std::vector<Node*>& value) override { }
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual bool configureAttribute(
      const ConfigureContext& ctx, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual BuildValue getResultForOutput(Node* node,
                                        const BuildValue& value) override {
    // This method should never be called on a custom command.
    llvm_unreachable("unexpected");
    return BuildValue::makeInvalid();
  }
  
  virtual bool isResultValid(BuildSystem&, const BuildValue& value) override {
    // Always rebuild this task.
    return false;
  }

  virtual void start(BuildSystemCommandInterface& bsci,
                     core::Task* task) override { }
  virtual void providePriorValue(BuildSystemCommandInterface&, core::Task*,
                                 const BuildValue&) override { }
  virtual void provideValue(BuildSystemCommandInterface& bsci, core::Task*,
                            uintptr_t inputID,
                            const BuildValue& value) override { }

  virtual BuildValue execute(BuildSystemCommandInterface& bsci,
                             core::Task* task,
                             QueueJobContext* context) override {
    // Construct the command line used to query the swift compiler version.
    //
    // FIXME: Need a decent subprocess interface.
    SmallString<256> command;
    llvm::raw_svector_ostream commandOS(command);
    commandOS << executable;
    commandOS << " " << "--version";

    // Read the result.
    FILE *fp = basic::sys::popen(commandOS.str().str().c_str(), "r");
    SmallString<4096> result;
    if (fp) {
      char buf[4096];
      for (;;) {
        ssize_t numRead = fread(buf, 1, sizeof(buf), fp);
        if (numRead == 0) {
          // FIXME: Error handling.
          break;
        }
        result.append(StringRef(buf, numRead));
      }
      basic::sys::pclose(fp);
    }

    // For now, we can get away with just encoding this as a successful
    // command and relying on the signature to detect changes.
    //
    // FIXME: We should support BuildValues with arbitrary payloads.
    return BuildValue::makeSuccessfulCommand(
        basic::FileInfo{}, basic::hashString(result));
  }
};

class SwiftCompilerShellCommand : public ExternalCommand {
  /// The compiler command to invoke.
  std::string executable = "swiftc";
  
  /// The name of the module.
  std::string moduleName;
  
  /// The path of the output module.
  std::string moduleOutputPath;

  /// The list of sources (combined).
  std::vector<std::string> sourcesList;

  /// The list of objects (combined).
  std::vector<std::string> objectsList;

  /// The list of import paths (combined).
  std::vector<std::string> importPaths;

  /// The directory in which to store temporary files.
  std::string tempsPath;

  /// Additional arguments, as a string.
  std::vector<std::string> otherArgs;

  /// Whether the sources are part of a library or not.
  bool isLibrary = false;
  
  /// Whether to enable -whole-module-optimization.
  bool enableWholeModuleOptimization = false;

  /// Enables multi-threading with the thread count if > 0.
  std::string numThreads = "0";

  virtual uint64_t getSignature() override {
    // FIXME: Use a more appropriate hashing infrastructure.
    using llvm::hash_combine;
    llvm::hash_code code = ExternalCommand::getSignature();
    code = hash_combine(code, executable);
    code = hash_combine(code, moduleName);
    code = hash_combine(code, moduleOutputPath);
    for (const auto& item: sourcesList) {
      code = hash_combine(code, item);
    }
    for (const auto& item: objectsList) {
      code = hash_combine(code, item);
    }
    for (const auto& item: importPaths) {
      code = hash_combine(code, item);
    }
    code = hash_combine(code, tempsPath);
    for (const auto& item: otherArgs) {
      code = hash_combine(code, item);
    }
    code = hash_combine(code, isLibrary);
    return size_t(code);
  }

  /// Get the path to use for the output file map.
  void getOutputFileMapPath(SmallVectorImpl<char>& result) const {
    llvm::sys::path::append(result, tempsPath, "output-file-map.json");
  }
    
  /// Compute the complete set of command line arguments to invoke swift with.
  void constructCommandLineArgs(StringRef outputFileMapPath,
                                std::vector<StringRef>& result) const {
    result.push_back(executable);
    result.push_back("-module-name");
    result.push_back(moduleName);
    result.push_back("-incremental");
    result.push_back("-emit-dependencies");
    if (!moduleOutputPath.empty()) {
      result.push_back("-emit-module");
      result.push_back("-emit-module-path");
      result.push_back(moduleOutputPath);
    }
    result.push_back("-output-file-map");
    result.push_back(outputFileMapPath);
    if (isLibrary) {
      result.push_back("-parse-as-library");
    }
    if (enableWholeModuleOptimization) {
      result.push_back("-whole-module-optimization");
    }
    result.push_back("-num-threads");
    result.push_back(numThreads);
    result.push_back("-c");
    for (const auto& source: sourcesList) {
      result.push_back(source);
    }
    for (const auto& import: importPaths) {
      result.push_back("-I");
      result.push_back(import);
    }
    for (const auto& arg: otherArgs) {
      result.push_back(arg);
    }
  }
  
public:
  using ExternalCommand::ExternalCommand;

  virtual void getShortDescription(SmallVectorImpl<char> &result) override {
      llvm::raw_svector_ostream(result)
        << "Compile Swift Module '" << moduleName
        << "' (" << sourcesList.size() << " sources)";
  }

  virtual void getVerboseDescription(SmallVectorImpl<char> &result) override {
    SmallString<64> outputFileMapPath;
    getOutputFileMapPath(outputFileMapPath);
    
    std::vector<StringRef> commandLine;
    constructCommandLineArgs(outputFileMapPath, commandLine);
    
    llvm::raw_svector_ostream os(result);
    bool first = true;
    for (const auto& arg: commandLine) {
      if (!first) os << " ";
      first = false;
      // FIXME: This isn't correct, we need utilities for doing shell quoting.
      if (arg.find(' ') != StringRef::npos) {
        os << '"' << arg << '"';
      } else {
        os << arg;
      }
    }
  }
  
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    if (name == "executable") {
      executable = value;
    } else if (name == "module-name") {
      moduleName = value;
    } else if (name == "module-output-path") {
      moduleOutputPath = value;
    } else if (name == "sources") {
      SmallVector<StringRef, 32> sources;
      StringRef(value).split(sources, " ", /*MaxSplit=*/-1,
                             /*KeepEmpty=*/false);
      sourcesList = std::vector<std::string>(sources.begin(), sources.end());
    } else if (name == "objects") {
      SmallVector<StringRef, 32> objects;
      StringRef(value).split(objects, " ", /*MaxSplit=*/-1,
                             /*KeepEmpty=*/false);
      objectsList = std::vector<std::string>(objects.begin(), objects.end());
    } else if (name == "import-paths") {
      SmallVector<StringRef, 32> imports;
      StringRef(value).split(imports, " ", /*MaxSplit=*/-1,
                             /*KeepEmpty=*/false);
      importPaths = std::vector<std::string>(imports.begin(), imports.end());
    } else if (name == "temps-path") {
      tempsPath = value;
    } else if (name == "is-library") {
      if (!configureBool(ctx, isLibrary, name, value))
        return false;
    } else if (name == "enable-whole-module-optimization") {
      if (!configureBool(ctx, enableWholeModuleOptimization, name, value))
        return false;
    } else if (name == "num-threads") {
      int numThreadsInt = 0;
      if (value.getAsInteger(10, numThreadsInt)) {
        ctx.error("'" + name + "' should be an int.");
        return false;
      }
      if (numThreadsInt < 0) {
        ctx.error("'" + name + "' should be greater than or equal to zero.");
        return false;
      }
      numThreads = value;
    } else if (name == "other-args") {
      SmallVector<StringRef, 32> args;
      StringRef(value).split(args, " ", /*MaxSplit=*/-1,
                             /*KeepEmpty=*/false);
      otherArgs = std::vector<std::string>(args.begin(), args.end());
    } else {
      return ExternalCommand::configureAttribute(ctx, name, value);
    }

    return true;
  }

  // Extracts and stores the bool value of an attribute inside "to" variable.
  // Returns true on success and false on error.
  bool configureBool(const ConfigureContext& ctx, bool& to, StringRef name, StringRef value) {
    if (value != "true" && value != "false") {
      ctx.error("invalid value: '" + value + "' for attribute '" +
          name + "'");
      return false;
    }
    to = value == "true";
    return true;
  }

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    if (name == "sources") {
      sourcesList = std::vector<std::string>(values.begin(), values.end());
    } else if (name == "objects") {
      objectsList = std::vector<std::string>(values.begin(), values.end());
    } else if (name == "import-paths") {
      importPaths = std::vector<std::string>(values.begin(), values.end());
    } else if (name == "other-args") {
      otherArgs = std::vector<std::string>(values.begin(), values.end());
    } else {
      return ExternalCommand::configureAttribute(ctx, name, values);
    }
    
    return true;
  }

  virtual bool configureAttribute(
      const ConfigureContext& ctx, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override {
    return ExternalCommand::configureAttribute(ctx, name, values);
  }

  bool writeOutputFileMap(BuildSystemCommandInterface& bsci,
                          StringRef outputFileMapPath,
                          std::vector<std::string>& depsFiles_out) const {
    // FIXME: We need to properly escape everything we write here.
    assert(sourcesList.size() == objectsList.size());
    
    SmallString<16> data;
    std::error_code ec;
    llvm::raw_fd_ostream os(outputFileMapPath, ec,
                            llvm::sys::fs::OpenFlags::F_Text);
    if (ec) {
      bsci.getDelegate().error(
          "", {},
          "unable to create output file map: '" + outputFileMapPath + "'");
      return false;
    }

    os << "{\n";
    
    // Write the master file dependencies entry.
    SmallString<16> masterDepsPath;
    llvm::sys::path::append(masterDepsPath, tempsPath, "master.swiftdeps");
    os << "  \"\": {\n";
    if (enableWholeModuleOptimization) {
      SmallString<16> depsPath;
      llvm::sys::path::append(depsPath, tempsPath, moduleName + ".d");
      depsFiles_out.push_back(depsPath.str());
      SmallString<16> object;
      llvm::sys::path::append(object, tempsPath, moduleName + ".o");
      os << "    \"dependencies\": \"" << depsPath << "\",\n";
      os << "    \"object\": \"" << object << "\",\n";
    }
    os << "    \"swift-dependencies\": \"" << masterDepsPath << "\"\n";
    os << "  },\n";

    // Write out the entries for each source file.
    for (unsigned i = 0; i != sourcesList.size(); ++i) {
      auto source = sourcesList[i];
      auto object = objectsList[i];
      auto sourceStem = llvm::sys::path::stem(source);
      SmallString<16> partialModulePath;
      llvm::sys::path::append(partialModulePath, tempsPath,
                              sourceStem + "~partial.swiftmodule");
      SmallString<16> swiftDepsPath;
      llvm::sys::path::append(swiftDepsPath, tempsPath,
                              sourceStem + ".swiftdeps");
      
      os << "  \"" << source << "\": {\n";
      if (!enableWholeModuleOptimization) {
        SmallString<16> depsPath;
        llvm::sys::path::append(depsPath, tempsPath, sourceStem + ".d");
        os << "    \"dependencies\": \"" << depsPath << "\",\n";
        depsFiles_out.push_back(depsPath.str());
      }
      os << "    \"object\": \"" << object << "\",\n";
      os << "    \"swiftmodule\": \"" << partialModulePath << "\",\n";
      os << "    \"swift-dependencies\": \"" << swiftDepsPath << "\"\n";
      os << "  }" << ((i + 1) < sourcesList.size() ? "," : "") << "\n";
    }
    
    os << "}\n";
    
    os.close();

    return true;
  }

  bool processDiscoveredDependencies(BuildSystemCommandInterface& bsci,
                                     core::Task* task, StringRef depsPath) {
    // Read the dependencies file.
    auto input = bsci.getDelegate().getFileSystem().getFileContents(depsPath);
    if (!input) {
      bsci.getDelegate().error(
          "", {},
          "unable to open dependencies file '" + depsPath + "'");
      return false;
    }

    // Parse the output.
    //
    // We just ignore the rule, and add any dependency that we encounter in the
    // file.
    struct DepsActions : public core::MakefileDepsParser::ParseActions {
      BuildSystemCommandInterface& bsci;
      core::Task* task;
      StringRef depsPath;
      unsigned numErrors{0};
      unsigned ruleNumber{0};
      
      DepsActions(BuildSystemCommandInterface& bsci, core::Task* task,
                  StringRef depsPath)
          : bsci(bsci), task(task), depsPath(depsPath) {}

      virtual void error(const char* message, uint64_t position) override {
        bsci.getDelegate().error(
            "", {},
            "error reading dependency file: '" + depsPath + "' (" +
            std::string(message) + ")");
        ++numErrors;
      }

      virtual void actOnRuleDependency(const char* dependency,
                                       uint64_t length,
                                       const StringRef unescapedWord) override {
        // Only process dependencies for the first rule (the output file), the
        // rest are identical.
        if (ruleNumber == 0) {
          bsci.taskDiscoveredDependency(
              task, BuildKey::makeNode(unescapedWord));
        }
      }

      virtual void actOnRuleStart(const char* name, uint64_t length,
                                  const StringRef unescapedWord) override {}

      virtual void actOnRuleEnd() override {
        ++ruleNumber;
      }
    };

    DepsActions actions(bsci, task, depsPath);
    core::MakefileDepsParser(input->getBufferStart(), input->getBufferSize(),
                             actions).parse();
    return actions.numErrors == 0;
  }

  /// Overridden start to also introduce a dependency on the Swift compiler
  /// version.
  virtual void start(BuildSystemCommandInterface& bsci,
                     core::Task* task) override {
    ExternalCommand::start(bsci, task);
    
    // The Swift compiler version is also an input.
    //
    // FIXME: We need to fix the input ID situation, this is not extensible. We
    // either have to build a registration of the custom tasks so they can divy
    // up the input ID namespace, or we should just use the keys. Probably move
    // to just using the keys, unless there is a place where that is really not
    // cheap.
    auto getVersionKey = BuildKey::makeCustomTask(
        "swift-get-version", executable);
    bsci.taskNeedsInput(task, getVersionKey,
                        core::BuildEngine::kMaximumInputID - 1);
  }

  /// Overridden to access the Swift compiler version.
  virtual void provideValue(BuildSystemCommandInterface& bsci,
                            core::Task* task,
                            uintptr_t inputID,
                            const BuildValue& value) override {
    // We can ignore the 'swift-get-version' input, it is just used to detect
    // that we need to rebuild.
    if (inputID == core::BuildEngine::kMaximumInputID - 1) {
      return;
    }
    
    ExternalCommand::provideValue(bsci, task, inputID, value);
  }

  virtual CommandResult executeExternalCommand(BuildSystemCommandInterface& bsci,
                                               core::Task* task,
                                               QueueJobContext* context) override {
    // FIXME: Need to add support for required parameters.
    if (sourcesList.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'sources'");
      return CommandResult::Failed;
    }
    if (objectsList.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'objects'");
      return CommandResult::Failed;
    }
    if (moduleName.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'module-name'");
      return CommandResult::Failed;
    }
    if (tempsPath.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'temps-path'");
      return CommandResult::Failed;
    }

    if (sourcesList.size() != objectsList.size()) {
      bsci.getDelegate().error(
          "", {}, "'sources' and 'objects' are not the same size");
      return CommandResult::Failed;
    }

    // Ensure the temporary directory exists.
    //
    // We ignore failures here, and just let things that depend on this fail.
    //
    // FIXME: This should really be done using an additional implicit input, so
    // it only happens once per build.
    (void) bsci.getDelegate().getFileSystem().createDirectories(tempsPath);
 
    SmallString<64> outputFileMapPath;
    getOutputFileMapPath(outputFileMapPath);
    
    // Form the complete command.
    std::vector<StringRef> commandLine;
    constructCommandLineArgs(outputFileMapPath, commandLine);

    // Write the output file map.
    std::vector<std::string> depsFiles;
    if (!writeOutputFileMap(bsci, outputFileMapPath, depsFiles))
      return CommandResult::Failed;

    // Execute the command.
    auto result = bsci.getExecutionQueue().executeProcess(context, commandLine);

    if (result != CommandResult::Succeeded) {
      // If the command failed, there is no need to gather dependencies.
      return result;
    }

    // Load all of the discovered dependencies.
    for (const auto& depsPath: depsFiles) {
      if (!processDiscoveredDependencies(bsci, task, depsPath))
        return CommandResult::Failed;
    }
    
    return result;
  }
};

class SwiftCompilerTool : public Tool {
public:
  SwiftCompilerTool(StringRef name) : Tool(name) {}

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
  virtual bool configureAttribute(
      const ConfigureContext&ctx, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override {
    // No supported attributes.
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }

  virtual std::unique_ptr<Command> createCommand(StringRef name) override {
    return llvm::make_unique<SwiftCompilerShellCommand>(name);
  }

  virtual std::unique_ptr<Command>
  createCustomCommand(const BuildKey& key) override {
    if (key.getCustomTaskName() == "swift-get-version" ) {
      return llvm::make_unique<SwiftGetVersionCommand>(key);
    }

    return nullptr;
  }
};
}

std::unique_ptr<Tool> buildsystem::createSwiftCompilerTool(StringRef name) {
  return llvm::make_unique<SwiftCompilerTool>(name);
}
