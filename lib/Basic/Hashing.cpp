//===-- Hashing.cpp -------------------------------------------------------===//
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

#include "llbuild/Basic/Hashing.h"

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/Hashing.h"

namespace llbuild {
namespace basic {

uint64_t hashString(StringRef value) {
  return hash_value(value);
}

}
}
