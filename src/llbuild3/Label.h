//===- Label.h --------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_LABEL_H
#define LLBUILD3_LABEL_H

#include <string>

#include "llbuild3/Label.pb.h"

namespace llbuild3 {

std::string labelAsCanonicalString(const Label& label);

}

#endif
