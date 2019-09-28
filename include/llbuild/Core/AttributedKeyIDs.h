//===- AttributedKeyIDs.h ---------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_ATTRIBUTEDKEYIDS_H
#define LLBUILD_CORE_ATTRIBUTEDKEYIDS_H

#include "llbuild/Core/KeyID.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace llbuild {
namespace core {

/// A data structure representing a set of tuples (KeyID, flag) in a somewhat
/// compact form.
// FIXME: At some point, figure out the optimal representation for this
// data structure, which is likely to be a lot of the resident memory size.
class AttributedKeyIDs {
  // Dependencies to be kept track of.
  std::vector<KeyID> keys;

  /// Flags associated with the keys.
  std::vector<bool> flags;

public:

  /// Clear the contents of the set.
  void clear() {
    keys.clear();
    flags.clear();
  }

  /// Check whether the set is empty.
  bool empty() const {
    return keys.empty();
  }

  /// Return the size of the set.
  size_t size() const {
    return keys.size();
  }

  /// Change the size of the set.
  void resize(size_t newSize) {
    keys.resize(newSize);
    flags.resize(newSize);
  }

  /// A return value for the subscript operator[].
  struct KeyIDAndFlag {
    KeyID keyID;
    bool flag;
  };

  KeyIDAndFlag operator[](size_t n) const {
    return {keys[n], flags[n]};
  }

  /// Store a new tuple under a known index.
  void set(size_t n, KeyID id, bool flag) {
    keys[n] = id;
    flags[n] = flag;
  }

  /// Add a given tuple at the end of the set.
  void push_back(KeyID id, bool flag) {
      keys.push_back(id);
      flags.push_back(flag);
  }

  /// Append the contents of the given set into the current set.
  void append(const AttributedKeyIDs &rhs) {
    keys.insert(keys.end(), rhs.keys.begin(), rhs.keys.end());
    flags.insert(flags.end(), rhs.flags.begin(), rhs.flags.end());
  }

public:

  struct const_iterator {
  protected:
    const AttributedKeyIDs &object;
    size_t index;

    const_iterator(const AttributedKeyIDs &object, size_t n)
      : object(object), index(n) { }
  public:
    friend AttributedKeyIDs;

    void operator++() { index++; }
    KeyIDAndFlag operator*() const {
      return object[index];
    }

    bool operator !=(const const_iterator& rhs) const {
      return index != rhs.index;
    }
  };

  const_iterator begin() const { return {*this, 0}; };
  const_iterator end() const { return {*this, keys.size()}; }

};

}
}

#endif
