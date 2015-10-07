//===- Hashing.h ------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_HASHING_H
#define LLBUILD_BASIC_HASHING_H

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

namespace llbuild {
namespace basic {

uint64_t hashString(StringRef value);

}
}

#endif
