//===- ActionCache.h --------------------------------------------*- C++ -*-===//
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

#ifndef TRITIUM_CORE_BUILDDB_H
#define TRITIUM_CORE_BUILDDB_H

#include "tritium/core/ActionCache.pb.h"
#include "tritium/core/CASObjectID.pb.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tritium {
namespace core {


class Key;
struct KeyID;

class ActionCache {
public:
  virtual ~ActionCache();

  /// Get the value for the
  virtual void get(const Key& key, KeyType type, std::function<void(std::optional<CASObjectID>)> result) = 0;

  virtual void update(const Key& key, KeyType type, CASObjectID value) = 0;

  /// Get a compact, unique ID for the given key.
  ///
  /// This method must be thread safe.
  ///
  /// The IDs returned by this method must be stable for the lifetime of the
  /// ActionCache instance with which the delegate has been attached.
  ///
  /// \param key [out] The key whose unique ID is being returned.
  virtual const KeyID getKeyID(const Key& key, KeyType type) = 0;

  /// Get the key corresponding to a key ID.
  ///
  /// This method must be thread safe, and must not fail.
  virtual Key getKeyForID(const KeyID key, KeyType type) = 0;
};


class Hasher {
  virtual ~Hasher();

  virtual void combine(uint64_t) = 0;
  virtual void combine(const std::string&) = 0;
};

class Key {
  ~Key();

  virtual void hashInto(Hasher&) const = 0;
};


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
template <> struct hash<tritium::core::KeyID> {
  std::size_t operator()(const tritium::core::KeyID & k) const {
    return std::hash<uint64_t>()(k.value());
  }
};
}

#endif
