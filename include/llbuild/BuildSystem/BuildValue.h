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
#include "llbuild/Basic/BinaryCoding.h"
#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/FileInfo.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

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

    /// A value produced by a command which was skipped.
    SkippedCommand,

    /// Sentinel value representing the result of "building" a top-level target.
    Target,
  };
  static StringRef stringForKind(Kind);

  friend struct basic::BinaryCodingTraits<BuildValue::Kind>;

  /// The kind of value.
  Kind kind = Kind::Invalid;

  /// The number of attached output infos.
  uint32_t numOutputInfos = 0;

  /// The command hash, for successful commands.
  uint64_t commandSignature = 0;

  union {
    /// The file info for the rule output, for existing inputs and successful
    /// commands with a single output.
    FileInfo asOutputInfo;

    /// The file info for successful commands with multiple outputs.
    FileInfo* asOutputInfos;
  } valueData = { {} };

  bool kindHasCommandSignature() const {
    return isSuccessfulCommand();
  }

  bool kindHasOutputInfo() const {
    return isExistingInput() || isSuccessfulCommand();
  }
  
private:
  // Copying is disabled.
  BuildValue(const BuildValue&) LLBUILD_DELETED_FUNCTION;
  void operator=(const BuildValue&) LLBUILD_DELETED_FUNCTION;

  BuildValue() {}
  BuildValue(basic::BinaryDecoder& decoder);
  BuildValue(Kind kind) : kind(kind) {
    valueData.asOutputInfo = {};
    commandSignature = 0;
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

  FileInfo& getNthOutputInfo(unsigned n) {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    assert(n < getNumOutputs());
    if (hasMultipleOutputs()) {
      return valueData.asOutputInfos[n];
    } else {
      assert(n == 0);
      return valueData.asOutputInfo;
    }
  }

public:
  // BuildValues can only be moved, not copied.
  BuildValue(BuildValue&& rhs) : numOutputInfos(rhs.numOutputInfos) {
    kind = rhs.kind;
    numOutputInfos = rhs.numOutputInfos;
    commandSignature = rhs.commandSignature;
    if (rhs.hasMultipleOutputs()) {
      valueData.asOutputInfos = rhs.valueData.asOutputInfos;
      rhs.valueData.asOutputInfos = nullptr;
    } else {
      valueData.asOutputInfo = rhs.valueData.asOutputInfo;
    }
  }
  BuildValue& operator=(BuildValue&& rhs) {
    if (this != &rhs) {
      // Release our resources.
      if (hasMultipleOutputs())
        delete[] valueData.asOutputInfos;

      // Move the data.
      kind = rhs.kind;
      numOutputInfos = rhs.numOutputInfos;
      commandSignature = rhs.commandSignature;
      if (rhs.hasMultipleOutputs()) {
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
  static BuildValue makeSkippedCommand() {
    return BuildValue(Kind::SkippedCommand);
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
  bool isPropagatedFailureCommand() const {
    return kind == Kind::PropagatedFailureCommand;
  }
  bool isCancelledCommand() const { return kind == Kind::CancelledCommand; }
  bool isSkippedCommand() const { return kind == Kind::SkippedCommand; }
  bool isTarget() const { return kind == Kind::Target; }

  bool hasMultipleOutputs() const {
    return numOutputInfos > 1;
  }

  unsigned getNumOutputs() const {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    return numOutputInfos;
  }

  const FileInfo& getOutputInfo() const {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    assert(!hasMultipleOutputs() &&
           "invalid call on result with multiple outputs");
    return valueData.asOutputInfo;
  }

  const FileInfo& getNthOutputInfo(unsigned n) const {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    assert(n < getNumOutputs());
    if (hasMultipleOutputs()) {
      return valueData.asOutputInfos[n];
    } else {
      assert(n == 0);
      return valueData.asOutputInfo;
    }
  }

  uint64_t getCommandSignature() const {
    assert(kindHasCommandSignature() && "invalid call for value kind");
    return commandSignature;
  }

  /// @}

  /// @name Conversion to core ValueType.
  /// @{

  static BuildValue fromData(const core::ValueType& value) {
    basic::BinaryDecoder decoder(StringRef((char*)value.data(), value.size()));
    return BuildValue(decoder);
  }
  core::ValueType toData() const;

  /// @}

  /// @name Debug Support
  /// @{

  void dump(raw_ostream& OS) const;

  /// @}
};

}

template<>
struct basic::BinaryCodingTraits<buildsystem::BuildValue::Kind> {
  typedef buildsystem::BuildValue::Kind Kind;
  
  static inline void encode(Kind& value, BinaryEncoder& encoder) {
    uint8_t tmp = uint8_t(value);
    assert(value == Kind(tmp));
    encoder.write(tmp);
  }
  static inline void decode(Kind& value, BinaryDecoder& decoder) {
    uint8_t tmp;
    decoder.read(tmp);
    value = Kind(tmp);
  }
};

inline buildsystem::BuildValue::BuildValue(basic::BinaryDecoder& decoder) {
  // Handle empty decode requests.
  if (decoder.isEmpty()) {
    kind = BuildValue::Kind::Invalid;
    return;
  }
  
  decoder.read(kind);
  if (kindHasCommandSignature())
    decoder.read(commandSignature);
  if (kindHasOutputInfo()) {
    decoder.read(numOutputInfos);
    if (numOutputInfos > 1) {
        valueData.asOutputInfos = new FileInfo[numOutputInfos];
    }
    for (uint32_t i = 0; i != numOutputInfos; ++i) {
      decoder.read(getNthOutputInfo(i));
    }
  }
  decoder.finish();
}

inline core::ValueType buildsystem::BuildValue::toData() const {
  basic::BinaryEncoder coder;
  coder.write(kind);
  if (kindHasCommandSignature())
    coder.write(commandSignature);
  if (kindHasOutputInfo()) {
    coder.write(numOutputInfos);
    for (uint32_t i = 0; i != numOutputInfos; ++i) {
      coder.write(getNthOutputInfo(i));
    }
  }
  return coder.contents();
}

}

#endif
