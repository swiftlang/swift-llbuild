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

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

namespace llbuild {
namespace CAS {

/// An abstract representation of a content-hash.
struct DataID {
  static constexpr size_t MaxIDLength = 127;

  /// The size of the id.
  uint8_t size;

  /// The bytes of the id.
  char id[MaxIDLength];

private:
  /// Used for sentinels.
  DataID(uint8_t size, char markerByte) : size(size) {
    id[0] = markerByte;
  }
  
public:
  /// Create a DataID from the given string.
  DataID(llvm::StringRef id_) {
    if (id_.size() == 0 || id_.size() > DataID::MaxIDLength) {
      llvm::report_fatal_error("invalid DataID");
    }
    size = id_.size();
    memcpy(id, id_.data(), size);
    memset(id + size, 0, sizeof(id) - size);
  }

  /// Get the ID as a StringRef.
  auto str() const -> llvm::StringRef {
    return llvm::StringRef(id, size);
  }

  /// Check if two DataIDs are equivalent.
  bool operator==(const DataID &other) const {
    // The second test here is to cover the sentinels.
    return str() == other.str() && id[0] == other.id[0];
  }

  bool operator!=(const DataID& rhs) const { return !(*this == rhs); }

  /// Return an empty DataID (for use as a sentinel).
  static auto empty() -> DataID {
    return DataID(0, 0);
  }

  /// Return an invalid DataID (for use as a sentinel).
  static auto invalid() -> DataID {
    return DataID(0, 1);
  }
};

}
}

// Provide DenseMapInfo for DataID.
namespace llvm {
  using namespace llbuild::CAS;
  template<> struct DenseMapInfo<llbuild::CAS::DataID> {
    static inline DataID getEmptyKey() { return DataID::empty(); }
    static inline DataID getTombstoneKey() { return DataID::invalid(); }
    static unsigned getHashValue(const DataID& value) {
      return (unsigned)(hash_value(value.str()));
    }
    static bool isEqual(const DataID& lhs, const DataID& rhs) {
      return lhs == rhs;
    }
  };
}

#endif
