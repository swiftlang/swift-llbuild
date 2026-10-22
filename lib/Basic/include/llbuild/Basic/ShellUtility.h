//===- ShellUtility.h -------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_SHELLUTILITY_H
#define LLBUILD_BASIC_SHELLUTILITY_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

namespace llbuild {
namespace basic {

#if defined(_WIN32)
/// Formats a command line using the Windows command line escaping rules a la a
/// reverse CommandLineToArgVW
///
/// \param args The arugments to escape
///
std::string formatWindowsCommandString(std::vector<std::string> args);
#endif

/// Appends a shell escaped string to an output stream.
/// For e.g. hello -> hello, hello$world -> 'hello$world', input A -> 'input A'
///
/// \param os Reference of the output stream to append to.
///
/// \param string The string to be escaped and appended.
///
void appendShellEscapedString(llvm::raw_ostream& os, llvm::StringRef string);

/// Creates and returns a shell escaped string of the input.
///
/// \param string The string to be escaped.
///
/// \returns escaped string.
std::string shellEscaped(llvm::StringRef string);

}
}

#endif
