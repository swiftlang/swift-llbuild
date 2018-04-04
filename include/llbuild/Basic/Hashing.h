//===- Hashing.h ------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_HASHING_H
#define LLBUILD_BASIC_HASHING_H

#include "llbuild/Basic/BinaryCoding.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"

namespace llbuild {
namespace basic {

uint64_t hashString(StringRef value);

class CommandSignature {
public:
  CommandSignature() = default;
  CommandSignature(StringRef string) {
    value = size_t(llvm::hash_value(string));
  }
  explicit CommandSignature(uint64_t sig) : value(sig) {}
  CommandSignature(const CommandSignature& other) = default;
  CommandSignature(CommandSignature&& other) = default;
  CommandSignature& operator=(const CommandSignature& other) = default;
  CommandSignature& operator=(CommandSignature&& other) = default;

  bool isNull() const { return value == 0; }

  bool operator==(const CommandSignature& other) const { return value == other.value; }
  bool operator!=(const CommandSignature& other) const { return value != other.value; }

  CommandSignature& combine(StringRef string) {
    // FIXME: Use a more appropriate hashing infrastructure.
    value = llvm::hash_combine(value, string);
    return *this;
  }

  CommandSignature& combine(const std::string &string) {
    // FIXME: Use a more appropriate hashing infrastructure.
    value = llvm::hash_combine(value, string);
    return *this;
  }

  CommandSignature& combine(bool b) {
    // FIXME: Use a more appropriate hashing infrastructure.
    value = llvm::hash_combine(value, b);
    return *this;
  }

  template <typename T>
  CommandSignature& combine(const std::vector<T>& list) {
    for (const auto& v: list) {
      combine(v);
    }
    return *this;
  }

  uint64_t value = 0;
};

template<>
struct BinaryCodingTraits<CommandSignature> {
  static inline void encode(const CommandSignature& value, BinaryEncoder& coder) {
    coder.write(value.value);
  }
  static inline void decode(CommandSignature& value, BinaryDecoder& coder) {
    coder.read(value.value);
  }
};

}
}

#endif
