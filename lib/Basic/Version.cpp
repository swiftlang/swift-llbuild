//===-- Version.cpp -------------------------------------------------------===//
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

#include "llbuild/Basic/Version.h"

#include <string>

namespace llbuild {

std::string getLLBuildFullVersion() {
#ifdef LLBUILD_VERSION_STRING
    return "LLBuild version 0.1 (" + std::string(LLBUILD_VERSION_STRING) + ")";
#else
    return "LLBuild version 0.1";
#endif
}

}
