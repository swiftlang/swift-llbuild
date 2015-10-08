//===- SwiftTools.h ---------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_SWIFTTOOLS_H
#define LLBUILD_BUILDSYSTEM_SWIFTTOOLS_H

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <memory>

namespace llbuild {
namespace buildsystem {

class Tool;

std::unique_ptr<Tool> createSwiftCompilerTool(StringRef name);

}
}

#endif
