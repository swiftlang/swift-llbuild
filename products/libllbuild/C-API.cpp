//===-- C-API.cpp ---------------------------------------------------------===//
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

// Include the public API.
#include <llbuild/llbuild.h>

#include "llbuild/Basic/Version.h"

using namespace llbuild;

/* Misc API */

const char* llb_get_full_version_string(void) {
  // Use a static local to store the version string, to avoid lifetime issues.
  static std::string versionString = getLLBuildFullVersion();

  return versionString.c_str();
}
