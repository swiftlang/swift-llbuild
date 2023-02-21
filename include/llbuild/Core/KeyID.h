//===- KeyID.h -----------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_CORE_KEYID_H
#define LLBUILD_CORE_KEYID_H

#include "llbuild/Basic/Compiler.h"

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/StringRef.h"

namespace llbuild {
namespace core {

/// The scalar identifier has the following properties:
///  1. Has to be able to store a pointer value.
///  2. Can be used to identify something, as well as point to something.
struct KeyID {
  typedef uint64_t ValueTy;

private:
  ValueTy _value;

  // Some values at the top of the pointer range are reserved.
  static constexpr ValueTy MaxValidID = (~(uint64_t)0) << 4;

public:

  /// Get the identifying part of the key (without the flags).
  uint64_t value() const {
    return _value;
  }

  /// Implicit conversion to the identifying part of the key.
  operator uint64_t() const {
    return value();
  }

  /// Default initialization to a completely bogus value.
  constexpr KeyID() : _value(0) { }

  /// Store a pointer value.
  explicit KeyID(const void * value_ptr) : _value((uintptr_t)value_ptr) {
    // Some values at the top of the pointer range are reserved.
    assert(_value < MaxValidID);
  }

  /// Check if two KeyIDs are equivalent.
  constexpr bool operator==(const KeyID &other) const {
    return (_value == other._value);
  }

  /// Used for tests to support comparisons with explicit number literals.
  constexpr bool operator ==(int rhs) const {
    return rhs >= 0 && value() == (uint64_t)rhs;
  }

  /// Representation that doesn't denote a user-supplied value.
  static constexpr KeyID novalue() { return KeyID(); }

  /// Representation that denotes an explicitly invalid value.
  static constexpr KeyID invalid() {
    KeyID k;
    k._value = ~0UL;
    return k;
  }

};

}
}

namespace std {
  template <> struct hash<llbuild::core::KeyID> {
    std::size_t operator()(const llbuild::core::KeyID & k) const {
      return std::hash<uint64_t>()(k.value());
    }
  };
}

// Provide DenseMapInfo for KeyIDs.
namespace llvm {
  using namespace llbuild::core;
  template<> struct DenseMapInfo<llbuild::core::KeyID> {
    static inline KeyID getEmptyKey() { return KeyID::novalue(); }
    static inline KeyID getTombstoneKey() { return KeyID::invalid(); }
    static unsigned getHashValue(const KeyID& Val) {
      return (unsigned)(Val.value() * 37UL);
    }
    static bool isEqual(const KeyID& LHS, const KeyID& RHS) {
      return LHS == RHS;
    }
  };
}

#endif
