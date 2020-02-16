//===- BuildValue.h ---------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
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
#include "llbuild/Basic/Hashing.h"
#include "llbuild/Basic/LLVM.h"
#include "llbuild/Basic/StringList.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

namespace llvm {
class raw_ostream;
}

namespace llbuild {
namespace buildsystem {

/// The BuildValue encodes the value space used by the BuildSystem when using
/// the core BuildEngine.
class BuildValue {
  using FileInfo = basic::FileInfo;

public:
  enum class Kind : uint32_t {
    /// An invalid value, for sentinel purposes.
    Invalid = 0,

    /// A value produced by a virtual input.
    VirtualInput,

    /// A value produced by an existing input file.
    ExistingInput,

    /// A value produced by a missing input file.
    MissingInput,

    /// The contents of a directory.
    DirectoryContents,

    /// The signature of a directories contents.
    DirectoryTreeSignature,

    /// The signature of a directories structure.
    DirectoryTreeStructureSignature,

    /// A value produced by stale file removal.
    StaleFileRemoval,

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

    /// The filtered contents of a directory.
    FilteredDirectoryContents,

    /// A value produced by a successful command with an output signature.
    SuccessfulCommandWithOutputSignature,
  };
  
private:
  static StringRef stringForKind(Kind);

  friend struct basic::BinaryCodingTraits<BuildValue::Kind>;

  /// The kind of value.
  Kind kind = Kind::Invalid;

  /// The number of attached output infos.
  uint32_t numOutputInfos = 0;

  /// A hash value (used by some build value types).
  basic::CommandSignature signature;

  /// The file info for successful commands with multiple outputs or a single output.
  FileInfo* outputInfos;

  /// String list storage.
  //
  // FIXME: We are currently paying the cost for carrying this around on every
  // value, which is very wasteful. We need to redesign this type to be
  // customized to each exact value.
  basic::StringList stringValues;

  bool kindHasSignature() const {
    return isDirectoryTreeSignature() || isDirectoryTreeStructureSignature() ||
        kind == Kind::SuccessfulCommandWithOutputSignature;
  }

  bool kindHasStringList() const {
    return isDirectoryContents() || isFilteredDirectoryContents() || isStaleFileRemoval();
  }

  bool kindHasOutputInfo() const {
    return isExistingInput() || isSuccessfulCommand() || isDirectoryContents();
  }
  
private:
  void operator=(const BuildValue&) LLBUILD_DELETED_FUNCTION;

  BuildValue() {}
  BuildValue(basic::BinaryDecoder& decoder);
  BuildValue(Kind kind, basic::CommandSignature signature = basic::CommandSignature())
      : kind(kind), signature(signature) { }
  BuildValue(Kind kind, ArrayRef<FileInfo> outputInfos,
             basic::CommandSignature signature = basic::CommandSignature())
      : kind(kind), numOutputInfos(outputInfos.size()),
        signature(signature)
  {
    assert(numOutputInfos >= 1);
    this->outputInfos = new FileInfo[numOutputInfos];
    for (uint32_t i = 0; i != numOutputInfos; ++i) {
      this->outputInfos[i] = outputInfos[i];
    }
  }
  
  /// Create a build value containing directory contents.
  BuildValue(Kind kind, FileInfo directoryInfo, ArrayRef<std::string> values)
      : BuildValue(kind, directoryInfo)
  {
    assert(kindHasStringList());

    stringValues = basic::StringList(values);
  }

  BuildValue(Kind kind, ArrayRef<std::string> values)
      : kind(kind), stringValues(values) {
    assert(kindHasStringList());
  }

  std::vector<StringRef> getStringListValues() const {
    assert(kindHasStringList());
    return stringValues.getValues();
  }

public:
  FileInfo& getNthOutputInfo(unsigned n) {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    assert(n < getNumOutputs());
    return outputInfos[n];
  }

  // BuildValues preferentially should be moved, not copied.
  BuildValue(BuildValue&& rhs) : numOutputInfos(rhs.numOutputInfos) {
    kind = rhs.kind;
    numOutputInfos = rhs.numOutputInfos;
    signature = rhs.signature;
    outputInfos = rhs.outputInfos;
    rhs.outputInfos = nullptr;
    if (rhs.kindHasStringList()) {
      stringValues = std::move(rhs.stringValues);
    }
  }
  BuildValue& operator=(BuildValue&& rhs) {
    if (this != &rhs) {
      // Release our resources.
      if (kindHasOutputInfo()) {
        delete[] outputInfos;
      }

      // Move the data.
      kind = rhs.kind;
      numOutputInfos = rhs.numOutputInfos;
      signature = rhs.signature;
      outputInfos = rhs.outputInfos;
      rhs.outputInfos = nullptr;
      if (rhs.kindHasStringList()) {
        stringValues = std::move(rhs.stringValues);
      }
    }
    return *this;
  }

  // While we generally prefer to only move build values, passing them to CAPI
  // clients requires that we produce a copy. To avoid unnecessary serialization
  // this method gives the CAPI a lower cost means to explicityly produce such a
  // copy.
  explicit BuildValue(const BuildValue& rhs) : numOutputInfos(rhs.numOutputInfos) {
    kind = rhs.kind;
    numOutputInfos = rhs.numOutputInfos;
    signature = rhs.signature;
    if (rhs.kindHasOutputInfo()) {
      auto newOutputInfos = new FileInfo[numOutputInfos];
      for (uint32_t i = 0; i < numOutputInfos; ++i) {
        newOutputInfos[i] = rhs.outputInfos[i];
      }
      outputInfos = newOutputInfos;
    }

    if (rhs.kindHasStringList()) {
      stringValues = basic::StringList(ArrayRef<StringRef>(rhs.stringValues.getValues()));
    }
  }

  ~BuildValue() {
    if (kindHasOutputInfo()) {
      delete[] outputInfos;
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
  static BuildValue makeDirectoryContents(FileInfo directoryInfo,
                                          ArrayRef<std::string> values) {
    return BuildValue(Kind::DirectoryContents, directoryInfo, values);
  }
  static BuildValue makeDirectoryTreeSignature(basic::CommandSignature signature) {
    return BuildValue(Kind::DirectoryTreeSignature, signature);
  }
  static BuildValue makeDirectoryTreeStructureSignature(basic::CommandSignature signature) {
    return BuildValue(Kind::DirectoryTreeStructureSignature, signature);
  }
  static BuildValue makeMissingOutput() {
    return BuildValue(Kind::MissingOutput);
  }
  static BuildValue makeFailedInput() {
    return BuildValue(Kind::FailedInput);
  }
  static BuildValue makeSuccessfulCommand(ArrayRef<FileInfo> outputInfos) {
    return BuildValue(Kind::SuccessfulCommand, outputInfos);
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
  static BuildValue makeStaleFileRemoval(ArrayRef<std::string> values) {
    return BuildValue(Kind::StaleFileRemoval, values);
  }
  static BuildValue makeFilteredDirectoryContents(ArrayRef<std::string> values) {
    return BuildValue(Kind::FilteredDirectoryContents, values);
  }
  static BuildValue makeSuccessfulCommandWithOutputSignature(ArrayRef<FileInfo> outputInfos, basic::CommandSignature signature) {
    return BuildValue(Kind::SuccessfulCommandWithOutputSignature, outputInfos, signature);
  }

  /// @}

  /// @name Accessors
  /// @{

  BuildValue::Kind getKind() const { return kind; }
  
  bool isInvalid() const { return kind == Kind::Invalid; }
  bool isVirtualInput() const { return kind == Kind::VirtualInput; }
  bool isExistingInput() const { return kind == Kind::ExistingInput; }
  bool isMissingInput() const { return kind == Kind::MissingInput; }

  bool isDirectoryContents() const { return kind == Kind::DirectoryContents; }
  bool isDirectoryTreeSignature() const {
    return kind == Kind::DirectoryTreeSignature;
  }
  bool isDirectoryTreeStructureSignature() const {
    return kind == Kind::DirectoryTreeStructureSignature;
  }
  bool isStaleFileRemoval() const { return kind == Kind::StaleFileRemoval; }
  
  bool isMissingOutput() const { return kind == Kind::MissingOutput; }
  bool isFailedInput() const { return kind == Kind::FailedInput; }
  bool isSuccessfulCommand() const {
    return kind == Kind::SuccessfulCommand ||
        kind == Kind::SuccessfulCommandWithOutputSignature;

  }
  bool isFailedCommand() const { return kind == Kind::FailedCommand; }
  bool isPropagatedFailureCommand() const {
    return kind == Kind::PropagatedFailureCommand;
  }
  bool isCancelledCommand() const { return kind == Kind::CancelledCommand; }
  bool isSkippedCommand() const { return kind == Kind::SkippedCommand; }
  bool isTarget() const { return kind == Kind::Target; }
  bool isFilteredDirectoryContents() const {
    return kind == Kind::FilteredDirectoryContents;
  }

  std::vector<StringRef> getDirectoryContents() const {
    assert((isDirectoryContents() || isFilteredDirectoryContents()) && "invalid call for value kind");
    return getStringListValues();
  }

  std::vector<StringRef> getStaleFileList() const {
    assert(isStaleFileRemoval() && "invalid call for value kind");
    return getStringListValues();
  }
  
  basic::CommandSignature getDirectoryTreeSignature() const {
    assert(isDirectoryTreeSignature() && "invalid call for value kind");
    return signature;
  }
  
  basic::CommandSignature getDirectoryTreeStructureSignature() const {
    assert(isDirectoryTreeStructureSignature() &&
           "invalid call for value kind");
    return signature;
  }

  bool hasMultipleOutputs() const {
    return numOutputInfos > 1;
  }

  unsigned getNumOutputs() const {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    return numOutputInfos;
  }

  const FileInfo& getOutputInfo() const {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    assert(1 == getNumOutputs() &&
           "invalid call on result with multiple outputs");
    return outputInfos[0];
  }

  const FileInfo& getNthOutputInfo(unsigned n) const {
    assert(kindHasOutputInfo() && "invalid call for value kind");
    assert(n < getNumOutputs());
    return outputInfos[n];
  }

  basic::CommandSignature getOutputSignature() const {
    assert(kind == Kind::SuccessfulCommandWithOutputSignature && "invalid call for value kind");
    return signature;
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
  
  static inline void encode(const Kind& value, BinaryEncoder& coder) {
    uint8_t tmp = uint8_t(value);
    assert(value == Kind(tmp));
    coder.write(tmp);
  }
  static inline void decode(Kind& value, BinaryDecoder& coder) {
    uint8_t tmp;
    coder.read(tmp);
    value = Kind(tmp);
  }
};

inline buildsystem::BuildValue::BuildValue(basic::BinaryDecoder& coder) {
  // Handle empty decode requests.
  if (coder.isEmpty()) {
    kind = BuildValue::Kind::Invalid;
    return;
  }
  
  coder.read(kind);
  if (kindHasSignature())
    coder.read(signature);
  if (kindHasOutputInfo()) {
    coder.read(numOutputInfos);
    outputInfos = new FileInfo[numOutputInfos];
    for (uint32_t i = 0; i != numOutputInfos; ++i) {
      coder.read(getNthOutputInfo(i));
    }
  }
  if (kindHasStringList()) {
    stringValues = basic::StringList(coder);
  }
  coder.finish();
}

inline core::ValueType buildsystem::BuildValue::toData() const {
  basic::BinaryEncoder coder;
  coder.write(kind);
  if (kindHasSignature())
    coder.write(signature);
  if (kindHasOutputInfo()) {
    coder.write(numOutputInfos);
    for (uint32_t i = 0; i != numOutputInfos; ++i) {
      coder.write(getNthOutputInfo(i));
    }
  }
  if (kindHasStringList()) {
    stringValues.encode(coder);
  }
  return coder.contents();
}

}

#endif
