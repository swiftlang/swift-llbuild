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

#include "llvm/ADT/StringRef.h"

#include "llbuild/Basic/LLVM.h"

#include <memory>
#include <string>
#include <utility>

namespace llbuild {
namespace ninja {

class Parser;
struct Token;

}

// FIXME: Move all of these things to a real home.
namespace commands {
namespace util {

std::string escapedString(const char* start, unsigned length);

std::string escapedString(const std::string& string);

void emitError(const std::string& filename, const std::string& message,
               const ninja::Token& at, const ninja::Parser* parser);

void emitError(const std::string& filename, const std::string& message,
               const char* position, unsigned length,
               StringRef buffer);

bool readFileContents(std::string path,
                      std::unique_ptr<char[]> *data_out,
                      uint64_t* size_out,
                      std::string* error_out);

}
}
}

#endif
