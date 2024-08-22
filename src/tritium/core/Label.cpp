//===- Label.cpp ------------------------------------------------*- C++ -*-===//
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

#include "tritium/core/Label.h"

namespace tritium {
namespace core {

std::string labelAsCanonicalString(const Label& label) {
  std::string result("//");

  // FIXME: Should this allow empty components?

  for (int i = 0; i < label.components_size(); i++) {
    auto component = label.components(i);

    // add separator for subsequent components
    if (i > 0) {
      result.append("/");
    }
    result.append(component);
  }

  if (label.name().size() > 0) {
    result.append(":");
    result.append(label.name());
  }

  return result;
}

} // namespace core
} // namespace tritium
