//===-- llbuild.cpp - llbuild Frontend Utillity ---------------------------===//
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

#include <cstdio>

using namespace llbuild;

int main() {
    // Print the version and exit.
    printf("%s\n", getLLBuildFullVersion().c_str());

    return 0;
}
