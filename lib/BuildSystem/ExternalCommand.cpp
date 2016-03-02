//===-- ExternalCommand.cpp -----------------------------------------------===//
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

#include "llbuild/BuildSystem/ExternalCommand.h"

#include "llbuild/Basic/Hashing.h"
#include "llbuild/BuildSystem/BuildExecutionQueue.h"
#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/BuildSystem/BuildNode.h"
#include "llbuild/BuildSystem/BuildSystemCommandInterface.h"
#include "llbuild/BuildSystem/BuildValue.h"

#include "llbuild/Basic/FileInfo.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/Twine.h"

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::buildsystem;

uint64_t ExternalCommand::getSignature() {
  uint64_t result = 0;
  for (const auto* input: inputs) {
    result ^= basic::hashString(input->getName());
  }
  for (const auto* output: outputs) {
    result ^= basic::hashString(output->getName());
  }
  if (allowMissingInputs) {
    result = ~result;
  }
  return result;
}

void ExternalCommand::configureDescription(const ConfigureContext&,
                                           StringRef value) {
  description = value;
}

void ExternalCommand::
configureInputs(const ConfigureContext&,
                const std::vector<Node*>& value) {
  inputs.reserve(value.size());
  for (auto* node: value) {
    inputs.emplace_back(static_cast<BuildNode*>(node));
  }
}

void ExternalCommand::
configureOutputs(const ConfigureContext&, const std::vector<Node*>& value) {
  outputs.reserve(value.size());
  for (auto* node: value) {
    outputs.emplace_back(static_cast<BuildNode*>(node));
  }
}

bool ExternalCommand::
configureAttribute(const ConfigureContext& ctx, StringRef name,
                   StringRef value) {
  if (name == "allow-missing-inputs") {
    if (value != "true" && value != "false") {
      ctx.error("invalid value: '" + value + "' for attribute '" +
                name + "'");
      return false;
    }
    allowMissingInputs = value == "true";
    return true;
  } else {
    ctx.error("unexpected attribute: '" + name + "'");
    return false;
  }
}
bool ExternalCommand::
configureAttribute(const ConfigureContext& ctx, StringRef name,
                   ArrayRef<StringRef> values) {
  ctx.error("unexpected attribute: '" + name + "'");
  return false;
}
bool ExternalCommand::configureAttribute(
    const ConfigureContext& ctx, StringRef name,
    ArrayRef<std::pair<StringRef, StringRef>> values) {
  ctx.error("unexpected attribute: '" + name + "'");
  return false;
}

BuildValue ExternalCommand::
getResultForOutput(Node* node, const BuildValue& value) {
  // If the value was a failed or skipped command, propagate the failure.
  if (value.isFailedCommand() || value.isSkippedCommand())
    return BuildValue::makeFailedInput();

  // Otherwise, we should have a successful command -- return the actual
  // result for the output.
  assert(value.isSuccessfulCommand());

  // If the node is virtual, the output is always a virtual input value.
  if (static_cast<BuildNode*>(node)->isVirtual()) {
    return BuildValue::makeVirtualInput();
  }
    
  // Find the index of the output node.
  //
  // FIXME: This is O(N). We don't expect N to be large in practice, but it
  // could be.
  auto it = std::find(outputs.begin(), outputs.end(), node);
  assert(it != outputs.end());
    
  auto idx = it - outputs.begin();
  assert(idx < value.getNumOutputs());

  auto& info = value.getNthOutputInfo(idx);
  if (info.isMissing())
    return BuildValue::makeMissingOutput();
    
  return BuildValue::makeExistingInput(info);
}
  
bool ExternalCommand::isResultValid(BuildSystem& system,
                                    const BuildValue& value) {
  // If the prior value wasn't for a successful command, recompute.
  if (!value.isSuccessfulCommand())
    return false;
    
  // If the command's signature has changed since it was built, rebuild.
  if (value.getCommandSignature() != getSignature())
    return false;

  // Check the timestamps on each of the outputs.
  for (unsigned i = 0, e = outputs.size(); i != e; ++i) {
    auto* node = outputs[i];

    // Ignore virtual outputs.
    if (node->isVirtual())
      continue;
      
    // Always rebuild if the output is missing.
    auto info = node->getFileInfo(system.getDelegate().getFileSystem());
    if (info.isMissing())
      return false;

    // Otherwise, the result is valid if the file information has not changed.
    if (value.getNthOutputInfo(i) != info)
      return false;
  }

  // Otherwise, the result is ok.
  return true;
}

void ExternalCommand::start(BuildSystemCommandInterface& bsci,
                            core::Task* task) {
  // Notify the client the command is preparing to run.
  bsci.getDelegate().commandPreparing(this);
    
  // Initialize the build state.
  shouldSkip = false;
  hasMissingInput = false;

  // Request all of the inputs.
  unsigned id = 0;
  for (auto it = inputs.begin(), ie = inputs.end(); it != ie; ++it, ++id) {
    bsci.taskNeedsInput(task, BuildKey::makeNode(*it), id);
  }
}

void ExternalCommand::providePriorValue(BuildSystemCommandInterface&,
                                        core::Task*,
                                        const BuildValue&) {
}

void ExternalCommand::provideValue(BuildSystemCommandInterface& bsci,
                                   core::Task*,
                                   uintptr_t inputID,
                                   const BuildValue& value) {
  // Process the input value to see if we should skip this command.

  // All direct inputs should be individual node values.
  assert(!value.hasMultipleOutputs());
  assert(value.isExistingInput() || value.isMissingInput() ||
         value.isMissingOutput() || value.isFailedInput() ||
         value.isVirtualInput());

  // Predicate for whether the input should cause the command to skip.
  auto shouldSkipForInput = [&] {
    // If the value is an existing or virtual input, we are always good.
    if (value.isExistingInput() || value.isVirtualInput())
      return false;

    // We explicitly allow running the command against a missing output, under
    // the expectation that responsibility for reporting this situation falls to
    // the command.
    //
    // FIXME: Eventually, it might be nice to harden the format so that we know
    // when an output was actually required versus optional.
    if (value.isMissingOutput())
      return false;

    // If the value is a missing input, but those are allowed, it is ok.
    if (allowMissingInputs && value.isMissingInput())
      return false;

    // For anything else, this is an error and the command should be skipped.
    return true;
  };

  // Check if we need to skip the command because of this input.
  if (shouldSkipForInput()) {
    shouldSkip = true;
    if (value.isMissingInput()) {
      hasMissingInput = true;

      // FIXME: Design the logging and status output APIs.
      bsci.getDelegate().error(
          "", {}, (Twine("missing input '") + inputs[inputID]->getName() +
                   "' and no rule to build it"));
    }
  }
}

void ExternalCommand::inputsAvailable(BuildSystemCommandInterface& bsci,
                                      core::Task* task) {
  // If the build should cancel, do nothing.
  if (bsci.getDelegate().isCancelled()) {
    bsci.taskIsComplete(task, BuildValue::makeSkippedCommand());
    return;
  }
    
  // If this command should be skipped, do nothing.
  if (shouldSkip) {
    // If this command had a failed input, treat it as having failed.
    if (hasMissingInput) {
      // FIXME: Design the logging and status output APIs.
      bsci.getDelegate().error(
          "", {}, (Twine("cannot build '") + outputs[0]->getName() +
                   "' due to missing input"));

      // Report the command failure.
      bsci.getDelegate().hadCommandFailure();
    }

    bsci.taskIsComplete(task, BuildValue::makeSkippedCommand());
    return;
  }
  assert(!hasMissingInput);
    
  // Suppress static analyzer false positive on generalized lambda capture
  // (rdar://problem/22165130).
#ifndef __clang_analyzer__
  auto fn = [this, &bsci=bsci, task](QueueJobContext* context) {
    // Notify the client the actual command body is going to run.
    bsci.getDelegate().commandStarted(this);

    // Invoke the external command.
    auto result = executeExternalCommand(bsci, task, context);
    
    // Notify the client the command is complete.
    bsci.getDelegate().commandFinished(this);
    
    // Process the result.
    if (!result) {
      bsci.getDelegate().hadCommandFailure();

      // If the command failed, the result is failure.
      bsci.taskIsComplete(task, BuildValue::makeFailedCommand());
      return;
    }

    // Capture the file information for each of the output nodes.
    //
    // FIXME: We need to delegate to the node here.
    SmallVector<FileInfo, 8> outputInfos;
    for (auto* node: outputs) {
      if (node->isVirtual()) {
        outputInfos.push_back(FileInfo{});
      } else {
        outputInfos.push_back(node->getFileInfo(
                                  bsci.getDelegate().getFileSystem()));
      }
    }
      
    // Otherwise, complete with a successful result.
    bsci.taskIsComplete(
        task, BuildValue::makeSuccessfulCommand(outputInfos, getSignature()));
  };
  bsci.addJob({ this, std::move(fn) });
#endif
}
