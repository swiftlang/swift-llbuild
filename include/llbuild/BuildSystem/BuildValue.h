//===- BuildValue.h ---------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDVALUE_H
#define LLBUILD_BUILDSYSTEM_BUILDVALUE_H

#include "llbuild/Core/BuildEngine.h"
#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/FileInfo.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class raw_ostream;
}

namespace llbuild {
namespace buildsystem {

/// The BuildValue encodes the value space used by the BuildSystem when using
/// the core BuildEngine.
class BuildValue {
  using FileInfo = basic::FileInfo;

  enum class Kind : uint32_t {
    /// An invalid value, for sentinel purposes.
    Invalid = 0,

    /// A value produced by a virtual input.
    VirtualInput,

    /// A value produced by an existing input file.
    ExistingInput,

    /// A value produced by a missing input file.
    MissingInput,

    /// A value produced by a command which succeeded, but whose output was
    /// missing.
    MissingOutput,

    /// A value for a produced output whose command failed or was cancelled.
    FailedInput,

    /// A value produced by a successful command.
    SuccessfulCommand,

    /// A value produced by a failing command.
    FailedCommand,

    /// A value produced by a command which was skipped because one of its
    /// dependencies failed.
    PropagatedFailureCommand,

    /// A value produced by a command which was cancelled.
    CancelledCommand,

    /// Sentinel value representing the result of "building" a top-level target.
    Target,
  };
  static StringRef stringForKind(Kind);

  /// The kind of value.
  Kind kind;

  /// The number of attached output infos.
  uint32_t numOutputInfos = 0;

  /// The command hash, for successful commands.
  uint64_t commandSignature;

  union {
    /// The file info for the rule output, for existing inputs and successful
    /// commands with a single output.
    FileInfo asOutputInfo;

    /// The file info for successful commands with multiple outputs.
    FileInfo* asOutputInfos;
  } valueData;

private:
  // Copying is disabled.
  BuildValue(const BuildValue&) LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildValue&) LLBUILD_DELETED_FUNCTION;

  BuildValue() {}
  BuildValue(Kind kind) : kind(kind) {
    valueData.asOutputInfo = {};
  }
  BuildValue(Kind kind, ArrayRef<FileInfo> outputInfos,
             uint64_t commandSignature = 0)
      : kind(kind), numOutputInfos(outputInfos.size()),
        commandSignature(commandSignature)
  {
    assert(numOutputInfos >= 1);
    if (numOutputInfos == 1) {
      valueData.asOutputInfo = outputInfos[0];
    } else {
      valueData.asOutputInfos = new FileInfo[numOutputInfos];
      for (uint32_t i = 0; i != numOutputInfos; ++i) {
        valueData.asOutputInfos[i] = outputInfos[i];
      }
    }
  }

public:
  // BuildValues can only be moved, not copied.
  BuildValue(BuildValue&& rhs) : numOutputInfos(rhs.numOutputInfos) {
    kind = rhs.kind;
    if (numOutputInfos == 1) {
      valueData.asOutputInfo = rhs.valueData.asOutputInfo;
    } else {
      valueData.asOutputInfos = rhs.valueData.asOutputInfos;
      rhs.valueData.asOutputInfos = nullptr;
    }
  }
  BuildValue &operator=(BuildValue&& rhs) {
    if (this != &rhs) {
      // Release our resources.
      if (hasMultipleOutputs())
        delete[] valueData.asOutputInfos;

      // Move the data.
      kind = rhs.kind;
      numOutputInfos = rhs.numOutputInfos;
      if (numOutputInfos > 1) {
        valueData.asOutputInfos = rhs.valueData.asOutputInfos;
        rhs.valueData.asOutputInfos = nullptr;
      } else {
        valueData.asOutputInfo = rhs.valueData.asOutputInfo;
      }
    }
    return *this;
  }
  ~BuildValue() {
    if (hasMultipleOutputs()) {
      delete[] valueData.asOutputInfos;
    }
  }

  /// @name Construction Functions
  /// @{

  static BuildValue makeInvalid() {
    return BuildValue(Kind::Invalid);
  }
  static BuildValue makeVirtualInput() {
    return BuildValue(Kind::VirtualInput);
  }
  static BuildValue makeExistingInput(FileInfo outputInfo) {
    assert(!outputInfo.isMissing());
    return BuildValue(Kind::ExistingInput, outputInfo);
  }
  static BuildValue makeMissingInput() {
    return BuildValue(Kind::MissingInput);
  }
  static BuildValue makeMissingOutput() {
    return BuildValue(Kind::MissingOutput);
  }
  static BuildValue makeFailedInput() {
    return BuildValue(Kind::FailedInput);
  }
  static BuildValue makeSuccessfulCommand(
      ArrayRef<FileInfo> outputInfos, uint64_t commandSignature) {
    return BuildValue(Kind::SuccessfulCommand, outputInfos, commandSignature);
  }
  static BuildValue makeFailedCommand() {
    return BuildValue(Kind::FailedCommand);
  }
  static BuildValue makePropagatedFailureCommand() {
    return BuildValue(Kind::PropagatedFailureCommand);
  }
  static BuildValue makeCancelledCommand() {
    return BuildValue(Kind::CancelledCommand);
  }
  static BuildValue makeTarget() {
    return BuildValue(Kind::Target);
  }

  /// @}

  /// @name Accessors
  /// @{

  bool isInvalid() const { return kind == Kind::Invalid; }
  bool isVirtualInput() const { return kind == Kind::VirtualInput; }
  bool isExistingInput() const { return kind == Kind::ExistingInput; }
  bool isMissingInput() const { return kind == Kind::MissingInput; }

  bool isMissingOutput() const { return kind == Kind::MissingOutput; }
  bool isFailedInput() const { return kind == Kind::FailedInput; }
  bool isSuccessfulCommand() const {return kind == Kind::SuccessfulCommand; }
  bool isFailedCommand() const { return kind == Kind::FailedCommand; }
  bool isPropagatedFailureCommand() const { return kind == Kind::PropagatedFailureCommand; }
  bool isCancelledCommand() const { return kind == Kind::CancelledCommand; }
  bool isTarget() const { return kind == Kind::Target; }

  bool hasMultipleOutputs() const {
    return numOutputInfos > 1;
  }

  unsigned getNumOutputs() const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    return numOutputInfos;
  }

  const FileInfo& getOutputInfo() const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    assert(!hasMultipleOutputs() &&
           "invalid call on result with multiple outputs");
    return valueData.asOutputInfo;
  }

  const FileInfo& getNthOutputInfo(unsigned n) const {
    assert((isExistingInput() || isSuccessfulCommand()) &&
           "invalid call for value kind");
    assert(n < getNumOutputs());
    if (hasMultipleOutputs()) {
      return valueData.asOutputInfos[n];
    } else {
      assert(n == 0);
      return valueData.asOutputInfo;
    }
  }

  uint64_t getCommandSignature() const {
    assert(isSuccessfulCommand() && "invalid call for value kind");
    return commandSignature;
  }

  /// @}

  /// @name Conversion to core ValueType.
  /// @{

  static BuildValue fromData(const core::ValueType& value) {
    BuildValue result;
    assert(value.size() >= sizeof(result));
    memcpy(&result, value.data(), sizeof(result));

    // If this result has multiple output values, deserialize them properly.
    if (result.numOutputInfos > 1) {
      assert(value.size() == (sizeof(result) +
                              result.numOutputInfos * sizeof(FileInfo)));
      result.valueData.asOutputInfos = new FileInfo[result.numOutputInfos];
      memcpy(result.valueData.asOutputInfos,
             value.data() + sizeof(result),
             result.numOutputInfos * sizeof(FileInfo));
    } else {
      assert(value.size() == sizeof(result));
    }

    return result;
  }

  core::ValueType toData() const {
    if (numOutputInfos > 1) {
      // FIXME: This could be packed one entry tighter.
      std::vector<uint8_t> result(sizeof(*this) +
                                  numOutputInfos * sizeof(FileInfo));
      memcpy(result.data(), this, sizeof(*this));
      memcpy(result.data() + sizeof(*this), valueData.asOutputInfos,
             numOutputInfos * sizeof(FileInfo));
      return result;
    } else {
      std::vector<uint8_t> result(sizeof(*this));
      memcpy(result.data(), this, sizeof(*this));
      return result;
    }
  }

  /// @}

  /// @name Debug Support
  /// @{

  void dump(raw_ostream& OS) const;

  /// @}
};

}
}

#endif
