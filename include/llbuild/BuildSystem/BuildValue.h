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

#include "llbuild/Basic/Compiler.h"
#include "llbuild/Basic/FileInfo.h"

// FIXME: Eliminate need for this include, if we could forward declare the value
// type.
#include "llbuild/Core/BuildEngine.h"

namespace llbuild {
namespace buildsystem {

/// The BuildValue encodes the value space used by the BuildSystem when using
/// the core BuildEngine.
struct BuildValue {
  using FileInfo = basic::FileInfo;
  
  enum class Kind : uint32_t {
    /// An invalid value, for sentinel purposes.
    Invalid = 0,

    /// A value produced by an existing input file.
    ExistingInput,

    /// A value produced by a missing input file.
    MissingInput,

    /// A value produced by a successful command.
    SuccessfulCommand,

    /// A value produced by a failing command.
    FailedCommand,
  };

  /// The kind of value.
  Kind kind;

  /// The information on the relevant output file, if used.
  FileInfo outputInfo;

private:
  BuildValue() {}
  BuildValue(Kind kind) : kind(kind), outputInfo() {}
  BuildValue(Kind kind, FileInfo outputInfo)
      : kind(kind), outputInfo(outputInfo) {}

public:
  /// @name Construction Functions
  /// @{

  static BuildValue makeExistingInput(FileInfo outputInfo) {
    return BuildValue(Kind::ExistingInput, outputInfo);
  }
  static BuildValue makeMissingInput() {
    return BuildValue(Kind::MissingInput);
  }
  static BuildValue makeSuccessfulCommand() {
    return BuildValue(Kind::SuccessfulCommand);
  }
  static BuildValue makeFailedCommand() {
    return BuildValue(Kind::FailedCommand);
  }

  /// @}

  /// @name Accessors
  /// @{

  bool isExistingInput() const { return kind == Kind::ExistingInput; }
  bool isMissingInput() const { return kind == Kind::MissingInput; }
  bool isSuccessfulCommand() const {return kind == Kind::SuccessfulCommand; }
  bool isFailedCommand() const { return kind == Kind::FailedCommand; }

  const FileInfo& getOutputInfo() const {
    assert(isExistingInput() && "invalid call for value kind");
    return outputInfo;
  }

  /// @}

  /// @name Conversion to core ValueType.
  /// @{

  static BuildValue fromValue(const core::ValueType& value) {
    BuildValue result;
    assert(value.size() == sizeof(result));
    memcpy(&result, value.data(), sizeof(result));
    return result;
  }

  core::ValueType toValue() {
    std::vector<uint8_t> result(sizeof(*this));
    memcpy(result.data(), this, sizeof(*this));
    return result;
  }

  /// @}
};

}
}

#endif
