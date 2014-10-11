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

std::string EscapedString(const char *Start, unsigned Length);

std::string EscapedString(const std::string& String);

void EmitError(const std::string& Filename, const std::string& Message,
               const ninja::Token& At, const ninja::Parser* Parser);

bool ReadFileContents(std::string Path,
                      std::unique_ptr<char[]> *Data_Out,
                      uint64_t* Size_Out,
                      std::string* Error_Out);

}
}
}

#endif
