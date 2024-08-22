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

#ifndef TRITIUM_CORE_LABEL_H
#define TRITIUM_CORE_LABEL_H

#include <string>

#include "tritium/core/Label.pb.h"

namespace tritium {
namespace core {

std::string labelAsCanonicalString(const Label& label);

}
}

#endif
