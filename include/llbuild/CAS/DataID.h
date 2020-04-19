//===- CAS.h ----------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_CAS_DATAID_H
#define LLBUILD_CAS_DATAID_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace llbuild {
namespace CAS {

/// An abstract representation of a content-hash.
struct DataID {
  static constexpr size_t MaxIDLength = 256;

  /// The size of the id.
  uint8_t size;

  /// The bytes of the id.
  char id[MaxIDLength];
    
public:
  /// Create a DataID from the given string.
  DataID(llvm::StringRef id_) {
    if (id_.size() > DataID::MaxIDLength) {
      llvm::report_fatal_error("invalid DataID (too large)");
    }
    size = id_.size();
    memcpy(id, id_.data(), size);
    memset(id + size, 0, sizeof(id) - size);
  }

  /// Get the ID as a StringRef.
  auto str() -> llvm::StringRef {
    return llvm::StringRef(id, size);
  }
};

}
}

#endif
