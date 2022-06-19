//===- DependencyKeyIDs.h ---------------------------------------*- C++ -*-===//
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
class DependencyKeyIDs {
  // Dependencies to be kept track of.
  std::vector<KeyID> keys;

  /// Flags about the dependency relation, by bit.
  ///
  /// value & 1: Flag indicating if the dependency invalidates the downstream task
  /// (value >> 1) & 1: Flag indicating if the dependency is valid for only the current build and discarded for incremental builds
  std::vector<uint8_t> flags;
  
private:
  bool singleUse(int index) const {
    return (flags[index] >> 1) & 1;
  }
  
  bool orderOnly(int index) const {
    return flags[index] & 1;
  }
  
public:

  /// Clear the contents of the set.
  void clear() {
    keys.clear();
    flags.clear();
  }
  
  /// Removes entries that are flagged as `singleUse`.
  void cleanSingleUseDependencies() {
    bool shouldClean = size() > 0;
    if (!shouldClean) {
      return;
    }
    
    // Go in reverse order to make
    for (int i = (int)size(); i > 0; i--) {
      int index = i - 1;
      if (singleUse(index) == true) {
        keys.erase(keys.begin() + index);
        flags.erase(flags.begin() + index);
      }
    }
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
  struct KeyIDAndFlags {
    KeyID keyID;
    bool orderOnly;
    bool singleUse;

    constexpr bool isBarrier() const {
      return keyID.isNoValue();
    }
  };

  KeyIDAndFlags operator[](size_t n) const {
    return {keys[n], orderOnly(n), singleUse(n)};
  }

  /// Store a new tuple under a known index.
  void set(size_t n, KeyID id, bool orderOnlyFlag, bool singleUseFlag) {
    keys[n] = id;
    flags[n] = (singleUseFlag << 1) | orderOnlyFlag;
  }

  /// Add a given tuple at the end of the set.
  void push_back(KeyID id, bool orderOnlyFlag, bool singleUseFlag) {
    keys.push_back(id);
    flags.push_back((singleUseFlag << 1) | orderOnlyFlag);
  }

  /// Append the contents of the given set into the current set.
  void append(const DependencyKeyIDs &rhs) {
    keys.insert(keys.end(), rhs.keys.begin(), rhs.keys.end());
    flags.insert(flags.end(), rhs.flags.begin(), rhs.flags.end());
  }

  void addBarrier() {
    if (keys.empty() || keys.back() == 0)
      return;

    keys.push_back(KeyID::novalue());
    flags.push_back(0);
  }

public:

  struct const_iterator {
  protected:
    const DependencyKeyIDs &object;
    size_t index;

    const_iterator(const DependencyKeyIDs &object, size_t n)
      : object(object), index(n) { }
  public:
    friend DependencyKeyIDs;

    void operator++() { index++; }
    KeyIDAndFlags operator*() const {
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
