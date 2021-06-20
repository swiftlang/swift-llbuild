//===- CommandUtil.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_NINJA_COMMANDUTIL_H
#define LLBUILD_NINJA_COMMANDUTIL_H

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <memory>
#include <string>
#include <utility>

namespace llvm {
class MemoryBuffer;
}

namespace llbuild {
namespace ninja {

class Parser;
struct Token;

}

// FIXME: Move all of these things to a real home.
namespace commands {
namespace util {

std::string escapedString(StringRef str);

void emitError(StringRef filename, StringRef message,
               const ninja::Token& at, const ninja::Parser* parser);

void emitError(StringRef filename, StringRef message,
               const char* position, unsigned length,
               StringRef buffer);

/// Load the contents of the given file. Relative files will be resolved using
/// the current working directory of the process.
llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>> readFileContents(
    StringRef filename);

}
}
}

#endif
