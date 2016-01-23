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

#include "llbuild/BuildSystem/BuildFile.h"
#include "llbuild/BuildSystem/BuildSystem.h"

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace llbuild {
namespace core {

class Task;
  
}
  
namespace buildsystem {

class BuildNode;
class BuildSystem;
struct QueueJobContext;
  
/// This is a base class for defining commands which are run externally to the
/// build system and interact using files. It defines common base behaviors
/// which make sense for all such tools.
class ExternalCommand : public Command {
  std::vector<BuildNode*> inputs;
  std::vector<BuildNode*> outputs;
  std::string description;

  // Build specific data.
  //
  // FIXME: We should probably factor this out somewhere else, so we can enforce
  // it is never used when initialized incorrectly.

  /// If true, the command should be skipped (because of an error in an input).
  bool shouldSkip = false;

  /// If true, the command had a missing input (this implies ShouldSkip is
  /// true).
  bool hasMissingInput = false;

protected:
  const std::vector<BuildNode*>& getInputs() { return inputs; }
  
  const std::vector<BuildNode*>& getOutputs() { return outputs; }
  
  StringRef getDescription() { return description; }

  /// This function must be overriden by subclasses for any additional keys.
  virtual uint64_t getSignature();

  /// Extension point for subclasses, to actually execute the command.
  virtual bool executeExternalCommand(BuildSystemCommandInterface& bsci,
                                      core::Task* task,
                                      QueueJobContext* context) = 0;
  
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

  virtual BuildValue getResultForOutput(Node* node,
                                        const BuildValue& value) override;
  
  virtual bool isResultValid(BuildSystem&, const BuildValue& value) override;

  virtual void start(BuildSystemCommandInterface& bsci,
                     core::Task* task) override;

  virtual void providePriorValue(BuildSystemCommandInterface&, core::Task*,
                                 const BuildValue&) override;

  virtual void provideValue(BuildSystemCommandInterface& bsci, core::Task*,
                            uintptr_t inputID,
                            const BuildValue& value) override;

  virtual void inputsAvailable(BuildSystemCommandInterface& bsci,
                               core::Task* task) override;
};

}
}

#endif
