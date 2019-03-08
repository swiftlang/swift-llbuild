//===-- ShellCommand.cpp --------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/BuildSystem/ShellCommand.h"

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildSystemCommandInterface.h"
#include "llbuild/Core/DependencyInfoParser.h"
#include "llbuild/Core/MakefileDepsParser.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

void ShellCommand::start(BuildSystemCommandInterface& bsci,
                                 core::Task* task) {
  // Resolve the plugin state.
  handler = bsci.resolveShellCommandHandler(this);

  // Delegate to handler, if used.
  if (handler) {
    handlerState = handler->start(bsci, this);
  }

  this->ExternalCommand::start(bsci, task);
}

CommandSignature ShellCommand::getSignature() {
  CommandSignature signature = cachedSignature;
  if (!signature.isNull())
    return signature;

  auto code = ExternalCommand::getSignature();
  if (!signatureData.empty()) {
    code = code.combine(signatureData);
  } else {
    for (const auto& arg: args) {
      code = code.combine(arg);
    }
    for (const auto& entry: env) {
      code = code.combine(entry.first);
      code = code.combine(entry.second);
    }
    for (const auto& path: depsPaths) {
      code = code.combine(path);
    }
    code = code.combine(int(depsStyle));
    code = code.combine(int(inheritEnv));
    code = code.combine(int(canSafelyInterrupt));
  }
  signature = code;
  if (signature.isNull()) {
    signature = CommandSignature(1);
  }
  cachedSignature = signature;
  return signature;
}

bool ShellCommand::processDiscoveredDependencies(BuildSystemCommandInterface& bsci,
                                                 Task* task,
                                                 QueueJobContext* context) {
  // It is an error if the dependencies style is not specified.
  //
  // FIXME: Diagnose this sooner.
  if (depsStyle == DepsStyle::Unused) {
    bsci.getDelegate().commandHadError(
        this, "missing required 'deps-style' specifier");
    return false;
  }

  for (const auto& depsPath: depsPaths) {
    // Read the dependencies file.
    auto input = bsci.getFileSystem().getFileContents(depsPath);
    if (!input) {
      bsci.getDelegate().commandHadError(
          this, "unable to open dependencies file (" + depsPath + ")");
      return false;
    }

    switch (depsStyle) {
    case DepsStyle::Unused:
      assert(0 && "unreachable");
      break;

    case DepsStyle::Makefile:
      if (!processMakefileDiscoveredDependencies(
              bsci, task, context, depsPath, input.get()))
        return false;
      continue;

    case DepsStyle::DependencyInfo:
      if (!processDependencyInfoDiscoveredDependencies(
              bsci, task, context, depsPath, input.get()))
        return false;
      continue;
    }
      
    llvm::report_fatal_error("unknown dependencies style");
  }

  return true;
}

bool ShellCommand::processMakefileDiscoveredDependencies(BuildSystemCommandInterface& bsci,
                                                         Task* task,
                                                         QueueJobContext* context,
                                                         StringRef depsPath,
                                                         llvm::MemoryBuffer* input) {
  // Parse the output.
  //
  // We just ignore the rule, and add any dependency that we encounter in the
  // file.
  struct DepsActions : public core::MakefileDepsParser::ParseActions {
    BuildSystemCommandInterface& bsci;
    Task* task;
    ShellCommand* command;
    StringRef depsPath;
    unsigned numErrors{0};

    DepsActions(BuildSystemCommandInterface& bsci, Task* task,
                ShellCommand* command, StringRef depsPath)
        : bsci(bsci), task(task), command(command), depsPath(depsPath) {}

    virtual void error(const char* message, uint64_t position) override {
      std::string msg;
      raw_string_ostream msgStream(msg);
      msgStream << "error reading dependency file '" << depsPath.str() << "': "
          << message << " at position " << position;
      bsci.getDelegate().commandHadError(command, msgStream.str());
      ++numErrors;
    }

    virtual void actOnRuleDependency(const char* dependency,
                                     uint64_t length,
                                     const StringRef unescapedWord) override {
      if (llvm::sys::path::is_absolute(unescapedWord)) {
        bsci.taskDiscoveredDependency(task, BuildKey::makeNode(unescapedWord));
        return;
      }

      // Generate absolute path
      //
      // NOTE: This is making the assumption that relative paths coming in a
      // dependency file are in relation to the explictly set working
      // directory, or the current working directory when it has not been set.
      SmallString<PATH_MAX> absPath = StringRef(command->workingDirectory);
      llvm::sys::path::append(absPath, unescapedWord);
      llvm::sys::fs::make_absolute(absPath);

      bsci.taskDiscoveredDependency(task, BuildKey::makeNode(absPath));
    }

    virtual void actOnRuleStart(const char* name, uint64_t length,
                                const StringRef unescapedWord) override {}

    virtual void actOnRuleEnd() override {}
  };

  DepsActions actions(bsci, task, this, depsPath);
  core::MakefileDepsParser(input->getBufferStart(), input->getBufferSize(),
                           actions).parse();
  return actions.numErrors == 0;
}

bool
ShellCommand::processDependencyInfoDiscoveredDependencies(BuildSystemCommandInterface& bsci,
                                                          Task* task,
                                                          QueueJobContext* context,
                                                          StringRef depsPath,
                                                          llvm::MemoryBuffer* input) {
  // Parse the output.
  //
  // We just ignore the rule, and add any dependency that we encounter in the
  // file.
  struct DepsActions : public core::DependencyInfoParser::ParseActions {
    BuildSystemCommandInterface& bsci;
    Task* task;
    ShellCommand* command;
    StringRef depsPath;
    unsigned numErrors{0};

    DepsActions(BuildSystemCommandInterface& bsci, Task* task,
                ShellCommand* command, StringRef depsPath)
        : bsci(bsci), task(task), command(command), depsPath(depsPath) {}

    virtual void error(const char* message, uint64_t position) override {
      bsci.getDelegate().commandHadError(
          command, ("error reading dependency file '" + depsPath.str() +
                    "': " + std::string(message)));
      ++numErrors;
    }

    // Ignore everything but actual inputs.
    virtual void actOnVersion(StringRef) override { }
    virtual void actOnMissing(StringRef) override { }
    virtual void actOnOutput(StringRef) override { }

    virtual void actOnInput(StringRef name) override {
      bsci.taskDiscoveredDependency(task, BuildKey::makeNode(name));
    }
  };

  DepsActions actions(bsci, task, this, depsPath);
  core::DependencyInfoParser(input->getBuffer(), actions).parse();
  return actions.numErrors == 0;
}

bool ShellCommand::configureAttribute(const ConfigureContext& ctx, StringRef name,
                                      StringRef value) {
  if (name == "args") {
    // When provided as a scalar string, we default to executing using the
    // shell.
    args.clear();
#if defined(_WIN32)
    args.push_back(ctx.getDelegate().getInternedString(
                       "C:\\windows\\system32\\cmd.exe"));
    args.push_back(ctx.getDelegate().getInternedString("/C"));
#else
    args.push_back(ctx.getDelegate().getInternedString("/bin/sh"));
    args.push_back(ctx.getDelegate().getInternedString("-c"));
#endif
    args.push_back(ctx.getDelegate().getInternedString(value));
  } else if (name == "signature") {
    signatureData = value;
  } else if (name == "deps") {
    depsPaths.clear();
    depsPaths.emplace_back(value);
  } else if (name == "deps-style") {
    if (value == "makefile") {
      depsStyle = DepsStyle::Makefile;
    } else if (value == "dependency-info") {
      depsStyle = DepsStyle::DependencyInfo;
    } else {
      ctx.error("unknown 'deps-style': '" + value + "'");
      return false;
    }
    return true;
  } else if (name == "can-safely-interrupt") {
    if (value != "true" && value != "false") {
      ctx.error("invalid value: '" + value + "' for attribute '" +
                name + "'");
      return false;
    }
    canSafelyInterrupt = value == "true";
  } else if (name == "inherit-env") {
    if (value != "true" && value != "false") {
      ctx.error("invalid value: '" + value + "' for attribute '" +
                name + "'");
      return false;
    }
    inheritEnv = value == "true";
  } else if (name == "working-directory") {
    // Ensure the working directory is absolute. This will make sure any
    // relative directories are interpreted as relative to the CWD at the time
    // the rule is defined.
    SmallString<PATH_MAX> wd = value;
    llvm::sys::fs::make_absolute(wd);
    workingDirectory = StringRef(wd);
  } else if (name == "control-enabled") {
    if (value != "true" && value != "false") {
      ctx.error("invalid value: '" + value + "' for attribute '" +
                name + "'");
      return false;
    }
    controlEnabled = value == "true";
  } else {
    return ExternalCommand::configureAttribute(ctx, name, value);
  }

  return true;
}
  
bool ShellCommand::configureAttribute(const ConfigureContext& ctx, StringRef name,
                                              ArrayRef<StringRef> values) {
  if (name == "args") {
    // Diagnose missing arguments.
    if (values.empty()) {
      ctx.error("invalid arguments for command '" + getName() + "'");
      return false;
    }

    args.clear();
    args.reserve(values.size());
    for (auto arg: values) {
      args.emplace_back(ctx.getDelegate().getInternedString(arg));
    }
  } else if (name == "deps") {
    depsPaths.clear();
    depsPaths.insert(depsPaths.begin(), values.begin(), values.end());
  } else {
    return ExternalCommand::configureAttribute(ctx, name, values);
  }

  return true;
}

bool ShellCommand::configureAttribute(
    const ConfigureContext& ctx, StringRef name,
    ArrayRef<std::pair<StringRef, StringRef>> values) {
  if (name == "env") {
    env.clear();
    env.reserve(values.size());
    for (const auto& entry: values) {
      env.emplace_back(
          std::make_pair(
              ctx.getDelegate().getInternedString(entry.first),
              ctx.getDelegate().getInternedString(entry.second)));
    }
  } else {
    return ExternalCommand::configureAttribute(ctx, name, values);
  }

  return true;
}

void ShellCommand::executeExternalCommand(
    BuildSystemCommandInterface& bsci,
    Task* task,
    QueueJobContext* context,
    llvm::Optional<ProcessCompletionFn> completionFn) {
  auto commandCompletionFn = [this, &bsci, task, completionFn](ProcessResult result) {
    if (result.status != ProcessStatus::Succeeded) {
      // If the command failed, there is no need to gather dependencies.
      if (completionFn.hasValue())
        completionFn.getValue()(result);
      return;
    }

    // Collect the discovered dependencies, if used.
    if (!depsPaths.empty()) {
      // FIXME: Really want this job to go into a high priority fifo queue
      // so as to not hold up downstream tasks.
      bsci.addJob({ this, [this, &bsci, task, completionFn, result](QueueJobContext* context) {
            if (!processDiscoveredDependencies(bsci, task, context)) {
              // If we were unable to process the dependencies output, report a
              // failure.
              if (completionFn.hasValue())
                completionFn.getValue()(ProcessStatus::Failed);
              return;
            }
            if (completionFn.hasValue())
              completionFn.getValue()(result);
          }});
      return;
    }

    if (completionFn.hasValue())
      completionFn.getValue()(result);
  };
      
  // Delegate to the handler, if present.
  if (handler) {
    // FIXME: We should consider making this interface capable of feeding
    // back the dependencies directly.
    //
    // FIXME: This needs to honor certain properties of the execution queue
    // (like indicating when the work starts and stops, and communicating with
    // the execution queue delegate controlling how output is handled). It could
    // be the case that this should actually be delegating this work to run on
    // the execution queue, and the queue handles the handoff.
    handler->execute(
        handlerState.get(), this, bsci, task, context, commandCompletionFn);
    return;
  }

  // Execute the command.
  bsci.getExecutionQueue().executeProcess(
      context, args, env,
      /*inheritEnvironment=*/inheritEnv,
      {canSafelyInterrupt, workingDirectory, controlEnabled},
      /*completionFn=*/{commandCompletionFn});
}
