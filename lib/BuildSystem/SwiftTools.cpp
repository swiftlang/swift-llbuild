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

#include "llbuild/BuildSystem/SwiftTools.h"

#include "llbuild/Basic/Hashing.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildSystemCommandInterface.h"
#include "llbuild/BuildSystem/ExternalCommand.h"
#include "llbuild/Core/MakefileDepsParser.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

namespace {

class SwiftCompilerShellCommand : public ExternalCommand {
  /// The compiler command to invoke.
  std::string executable = "swiftc";
  
  /// The name of the module.
  std::string moduleName;
  
  /// The path of the output module.
  std::string moduleOutputPath;

  /// The list of sources (combined).
  //
  // FIXME: This should be an actual list.
  std::string sourcesList;

  /// The list of objects (combined).
  //
  // FIXME: This should be an actual list.
  std::string objectsList;

  /// The list of import paths (combined).
  //
  // FIXME: This should be an actual list.
  std::string importPaths;

  /// The directory in which to store temporary files.
  std::string tempsPath;

  /// Additional arguments, as a string.
  //
  // FIXME: This should be an actual list.
  std::string otherArgs;

  /// Whether the sources are part of a library or not.
  bool isLibrary = false;
  
  virtual uint64_t getSignature() override {
    uint64_t result = ExternalCommand::getSignature();
    result ^= basic::hashString(executable);
    result ^= basic::hashString(moduleName);
    result ^= basic::hashString(moduleOutputPath);
    result ^= basic::hashString(sourcesList);
    result ^= basic::hashString(objectsList);
    result ^= basic::hashString(importPaths);
    result ^= basic::hashString(tempsPath);
    result ^= basic::hashString(otherArgs);
    result ^= isLibrary;
    return result;
  }

public:
  using ExternalCommand::ExternalCommand;
  
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override {
    if (name == "executable") {
      executable = value;
    } else if (name == "module-name") {
      moduleName = value;
    } else if (name == "module-output-path") {
      moduleOutputPath = value;
    } else if (name == "sources") {
      sourcesList = value;
    } else if (name == "objects") {
      objectsList = value;
    } else if (name == "import-paths") {
      importPaths = value;
    } else if (name == "temps-path") {
      tempsPath = value;
    } else if (name == "is-library") {
      if (value != "true" && value != "false") {
        ctx.error("invalid value: '" + value + "' for attribute '" +
                  name + "'");
        return false;
      }
      isLibrary = value == "true";
    } else if (name == "other-args") {
      otherArgs = value;
    } else {
      return ExternalCommand::configureAttribute(ctx, name, value);
    }

    return true;
  }

  bool writeOutputFileMap(BuildSystemCommandInterface& bsci,
                          StringRef outputFileMapPath,
                          ArrayRef<StringRef> sources,
                          ArrayRef<StringRef> objects,
                          std::vector<std::string>& depsFiles_out) const {
    // FIXME: We need to properly escape everything we write here.
    assert(sources.size() == objects.size());
    
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
    os << "    \"swift-dependencies\": \"" << masterDepsPath << "\"\n";
    os << "  },\n";

    // Write out the entries for each source file.
    for (unsigned i = 0; i != sources.size(); ++i) {
      auto source = sources[i];
      auto object = objects[i];
      auto sourceStem = llvm::sys::path::stem(source);
      SmallString<16> depsPath;
      llvm::sys::path::append(depsPath, tempsPath, sourceStem + ".d");
      SmallString<16> partialModulePath;
      llvm::sys::path::append(partialModulePath, tempsPath,
                              sourceStem + "~partial.swiftmodule");
      SmallString<16> swiftDepsPath;
      llvm::sys::path::append(swiftDepsPath, tempsPath,
                              sourceStem + ".swiftdeps");
      depsFiles_out.push_back(depsPath.str());
      
      os << "  \"" << source << "\": {\n";
      os << "    \"dependencies\": \"" << depsPath << "\",\n";
      os << "    \"object\": \"" << object << "\",\n";
      os << "    \"swiftmodule\": \"" << partialModulePath << "\",\n";
      os << "    \"swift-dependencies\": \"" << swiftDepsPath << "\"\n";
      os << "  }" << ((i + 1) < sources.size() ? "," : "") << "\n";
    }
    
    os << "}\n";
    
    os.close();

    return true;
  }

  bool processDiscoveredDependencies(BuildSystemCommandInterface& bsci,
                                     core::Task* task, StringRef depsPath) {
    // Read the dependencies file.
    auto res = llvm::MemoryBuffer::getFile(depsPath);
    if (auto ec = res.getError()) {
      bsci.getDelegate().error(
          "", {},
          "unable to open dependencies file '" + depsPath +
          "' (" + ec.message() + ")");
      return false;
    }
    std::unique_ptr<llvm::MemoryBuffer> input(res->release());

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
                                       uint64_t length) override {
        // Only process dependencies for the first rule (the output file), the
        // rest are identical.
        if (ruleNumber == 0) {
          bsci.taskDiscoveredDependency(
              task, BuildKey::makeNode(StringRef(dependency, length)));
        }
      }

      virtual void actOnRuleStart(const char* name, uint64_t length) override {}
      virtual void actOnRuleEnd() override {
        ++ruleNumber;
      }
    };

    DepsActions actions(bsci, task, depsPath);
    core::MakefileDepsParser(input->getBufferStart(), input->getBufferSize(),
                             actions).parse();
    return actions.numErrors == 0;
  }

  virtual bool executeExternalCommand(BuildSystemCommandInterface& bsci,
                                      core::Task* task,
                                      QueueJobContext* context) override {
    // FIXME: Need to add support for required parameters.
    if (sourcesList.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'sources'");
      return false;
    }
    if (objectsList.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'objects'");
      return false;
    }
    if (moduleName.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'module-name'");
      return false;
    }
    if (tempsPath.empty()) {
      bsci.getDelegate().error("", {}, "no configured 'temps-path'");
      return false;
    }

    // Get the list of sources.
    SmallVector<StringRef, 32> sources;
    StringRef(sourcesList).split(sources, " ", /*MaxSplit=*/-1,
                                 /*KeepEmpty=*/false);

    // Get the list of objects.
    SmallVector<StringRef, 32> objects;
    StringRef(objectsList).split(objects, " ", /*MaxSplit=*/-1,
                                 /*KeepEmpty=*/false);
    if (sources.size() != objects.size()) {
      bsci.getDelegate().error(
          "", {}, "'sources' and 'objects' are not the same size");
      return false;
    }

    // Get the list of objects.
    SmallVector<StringRef, 32> imports;
    StringRef(importPaths).split(imports, " ", /*MaxSplit=*/-1,
                                 /*KeepEmpty=*/false);

    // Ensure the temporary directory exists.
    //
    // We ignore failures here, and just let things that depend on this fail.
    //
    // FIXME: This should really be done using an additional implicit input, so
    // it only happens once per build.
    (void)llvm::sys::fs::create_directories(tempsPath, /*ignoreExisting=*/true);
    
    // Construct the output file map.
    SmallString<16> outputFileMapPath;
    llvm::sys::path::append(
        outputFileMapPath, tempsPath, "output-file-map.json");

    std::vector<std::string> depsFiles;
    if (!writeOutputFileMap(bsci, outputFileMapPath, sources, objects,
                            depsFiles))
      return false;
    
    // Form the complete command.
    SmallString<256> command;
    llvm::raw_svector_ostream commandOS(command);
    commandOS << executable;
    commandOS << " " << "-module-name" << " " << moduleName;
    commandOS << " " << "-incremental" << " " << "-emit-dependencies";
    if (!moduleOutputPath.empty()) {
      commandOS << " " << "-emit-module"
                << " " << "-emit-module-path" << " " << moduleOutputPath;
    }
    commandOS << " " << "-output-file-map" << " " << outputFileMapPath;
    if (isLibrary) {
      commandOS << " " << "-parse-as-library";
    }
    commandOS << " " << "-c";
    for (const auto& source: sources) {
      commandOS << " " << source;
    }
    for (const auto& import: imports) {
      commandOS << " " << "-I" << import;
    }
    if (!otherArgs.empty()) {
      commandOS << " " << otherArgs;
    }
    commandOS.flush();
      
    // Log the command.
    //
    // FIXME: Design the logging and status output APIs.
    if (!bsci.getDelegate().showVerboseStatus()) {
      fprintf(stdout, "Compiling Swift Module '%s' (%d sources)\n",
              moduleName.c_str(), int(sources.size()));
    } else {
      fprintf(stdout, "%s\n", command.c_str());
    }
    fflush(stdout);

    // Execute the command.
    if (!bsci.getExecutionQueue().executeShellCommand(context, command)) {
      // If the command failed, there is no need to gather dependencies.
      return false;
    }

    // Load all of the discovered dependencies.
    for (const auto& depsPath: depsFiles) {
      if (!processDiscoveredDependencies(bsci, task, depsPath))
        return false;
    }
    
    return true;
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

  virtual std::unique_ptr<Command> createCommand(StringRef name) override {
    return std::make_unique<SwiftCompilerShellCommand>(name);
  }
};
}

std::unique_ptr<Tool> buildsystem::createSwiftCompilerTool(StringRef name) {
  return std::make_unique<SwiftCompilerTool>(name);
}
