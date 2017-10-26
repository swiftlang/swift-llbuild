//===- POSIXEnvironment.h ---------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BUILDSYSTEM_PROCESSENVIRONMENT_H
#define LLBUILD_BUILDSYSTEM_PROCESSENVIRONMENT_H

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Hashing.h"

#include <unordered_set>
#include <utility>
#include <vector>

namespace std {
  template<> struct hash<llvm::StringRef> {
    size_t operator()(const llvm::StringRef& value) const {
      return size_t(hash_value(value));
    }
  };
}

namespace llbuild {
namespace buildsystem {

/// A helper class for constructing a POSIX-style environment.
class POSIXEnvironment {
  /// The actual environment, this is only populated once frozen.
  std::vector<const char*> env;

  /// The underlying string storage.
  //
  // FIXME: This is not efficient, we could store into a single allocation.
  std::vector<std::string> envStorage;

  /// The list of known keys in the environment.
  std::unordered_set<StringRef> keys{};

  /// Whether the environment pointer has been vended, and assignments can no
  /// longer be mutated.
  bool isFrozen = false;
    
public:
  POSIXEnvironment() {}

  /// Add a key to the environment, if missing.
  ///
  /// If the key has already been defined, it will **NOT** be inserted.
  void setIfMissing(StringRef key, StringRef value) {
    assert(!isFrozen);
    if (keys.insert(key).second) {
      llvm::SmallString<256> assignment;
      assignment += key;
      assignment += '=';
      assignment += value;
      assignment += '\0';
      envStorage.emplace_back(assignment.str());
    }
  }

  /// Get the envirnonment pointer.
  ///
  /// This pointer is only valid for the lifetime of the environment itself.
  const char* const* getEnvp() {
    isFrozen = true;

    // Form the final environment.
    env.clear();
    for (const auto& entry: envStorage) {
      env.emplace_back(entry.c_str());
    }
    env.emplace_back(nullptr);
    return env.data();
  }
};

}
}

#endif
