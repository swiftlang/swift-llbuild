//===- Version.h ------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_VERSION_H
#define LLBUILD_BASIC_VERSION_H

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <string>

namespace llbuild {

/// Get the version string.
///
/// \param productName The name of the product to embed in the string.
std::string getLLBuildFullVersion(StringRef productName = "llbuild");

}

#endif
