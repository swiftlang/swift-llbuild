//===- DependencyInfoParser.h -----------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_DEPENDENCYINFOPARSER_H
#define LLBUILD_CORE_DEPENDENCYINFOPARSER_H

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace llbuild {
namespace core {

/// Interface for parsing the "dependency info" format used by Darwin tools.
class DependencyInfoParser {
public:
  /// Delegate interface for parser behavior.
  struct ParseActions {
    virtual ~ParseActions();

    /// Called if an error is encountered in parsing the input.
    ///
    /// \param message A C-string text message including information on the
    /// error.
    ///
    /// \param position The approximate position of the error in the input
    /// buffer.
    virtual void error(const char* message, uint64_t position) = 0;

    /// Called when the version information is found.
    ///
    /// There can only ever be one version info record in the file.
    virtual void actOnVersion(StringRef) = 0;

    /// Called when an input is found.
    virtual void actOnInput(StringRef) = 0;

    /// Called when an output is found.
    virtual void actOnOutput(StringRef) = 0;

    /// Called when a missing file entry is found.
    ///
    /// These entries indicate a file which was looked for by the tool, but not
    /// found, and can be used to track anti-dependencies.
    virtual void actOnMissing(StringRef) = 0;
  };

  StringRef data;
  ParseActions& actions;
  
public:
  DependencyInfoParser(StringRef data, ParseActions& actions)
    : data(data), actions(actions) {}

  void parse();
};

}
}

#endif
