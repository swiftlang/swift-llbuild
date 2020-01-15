//===- ExternalCommand.h ----------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_EXTERNALCOMMAND_H
#define LLBUILD_BUILDSYSTEM_EXTERNALCOMMAND_H

#include "llbuild/BuildSystem/BuildDescription.h"
#include "llbuild/BuildSystem/BuildSystem.h"
#include "llbuild/BuildSystem/BuildValue.h"
#include "llbuild/BuildSystem/Command.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace llbuild {
  namespace basic {
    class QueueJobContext;
  }
  namespace core {
    class Task;
  }
  
namespace buildsystem {

class BuildNode;
class BuildSystem;
class BuildSystemDelegate;
class ExternalCommandHandler;

/// This is a base class for defining commands which are run externally to the
/// build system and interact using files. It defines common base behaviors
/// which make sense for all such tools.
class ExternalCommand : public Command {
  std::vector<BuildNode*> inputs;
  std::vector<BuildNode*> outputs;
  std::string description;

  /// Whether to allow missing inputs.
  bool allowMissingInputs = false;

  /// Whether to allow modified outputs.
  //
  // FIXME: This is currently useful as a mechanism for defining builds in which
  // an output is intentionally modified by another command. However, this isn't
  // a very robust mechanism, and we should ultimately move to a model whereby
  // we can reconstruct exactly the state of each individual command (by having
  // a durable storage for the outputs outside of the file system).
  bool allowModifiedOutputs = false;

  /// Whether to treat the command as always being out-of-date.
  bool alwaysOutOfDate = false;

  /// If not None, the command should be skipped with the provided BuildValue.
  llvm::Optional<BuildValue> skipValue;

  /// If there are any elements, the command had missing input nodes
  /// (this implies ShouldSkip is true).
  SmallPtrSet<Node*, 1> missingInputNodes;

  /// If true, the command can legally be updated if the output state allows it.
  bool canUpdateIfNewer = true;

  /// Whether a prior result has been found.
  bool hasPriorResult = false;
  
  /// Check if it is legal to only update the result (versus rerunning)
  /// because the outputs are newer than all of the inputs.
  bool canUpdateIfNewerWithResult(const BuildValue& result);

protected:
  const std::vector<BuildNode*>& getInputs() const { return inputs; }
  
  const std::vector<BuildNode*>& getOutputs() const { return outputs; }
  
  StringRef getDescription() const { return description; }

  /// This function must be overriden by subclasses for any additional keys.
  virtual basic::CommandSignature getSignature() const override;

  /// Extension point for subclases, to modify the command dependencies if needed.
  virtual void startExternalCommand(BuildSystem& system, core::TaskInterface&) = 0;

  /// Extension point for subclasses, to retrieve the BuildValue requested. May request new keys from this extension.
  virtual void provideValueExternalCommand(
      BuildSystem& system,
      core::TaskInterface& ti,
      uintptr_t inputID,
      const BuildValue& value) = 0;

  /// Extension point for subclasses, to actually execute the command after all dependencies have been calculated.
  virtual void executeExternalCommand(
      BuildSystem& system,
      core::TaskInterface& ti,
      basic::QueueJobContext* context,
      llvm::Optional<basic::ProcessCompletionFn> completionFn = {llvm::None}) = 0;
  
  /// Compute the output result for the command.
  virtual BuildValue computeCommandResult(BuildSystem& system, core::TaskInterface& ti);

public:
  using Command::Command;

  virtual void configureDescription(const ConfigureContext&,
                                    StringRef value) override;
  
  virtual void configureInputs(const ConfigureContext&,
                               const std::vector<Node*>& value) override;

  virtual void configureOutputs(const ConfigureContext&,
                                const std::vector<Node*>& value) override;

  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  StringRef value) override;
  virtual bool configureAttribute(const ConfigureContext& ctx, StringRef name,
                                  ArrayRef<StringRef> values) override;
  virtual bool configureAttribute(
      const ConfigureContext&, StringRef name,
      ArrayRef<std::pair<StringRef, StringRef>> values) override;

  virtual BuildValue getResultForOutput(Node* node,
                                        const BuildValue& value) override;
  
  virtual bool isResultValid(BuildSystem&, const BuildValue& value) override;

  virtual void start(BuildSystem& system, core::TaskInterface& ti) override;

  virtual void providePriorValue(BuildSystem& system,
                                 core::TaskInterface&,
                                 const BuildValue&) override;

  virtual void provideValue(BuildSystem& system,
                            core::TaskInterface& ti,
                            uintptr_t inputID,
                            const BuildValue& value) override;

  virtual void execute(BuildSystem& system,
                       core::TaskInterface&,
                       basic::QueueJobContext* context,
                       ResultFn resultFn) override;
};

}
}

#endif
