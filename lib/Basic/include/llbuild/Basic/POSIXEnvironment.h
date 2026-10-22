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

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace std {
template <> struct hash<llvm::StringRef> {
  size_t operator()(const llvm::StringRef& value) const {
    return size_t(hash_value(value));
  }
};
}

namespace llbuild {
namespace basic {

/// A helper class for constructing a POSIX-style environment.
class POSIXEnvironment {
  /// The actual environment, this is only populated once frozen.
#if defined(_WIN32)
  std::vector<wchar_t> env;
#else
  std::vector<const char*> env;
#endif

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

#if defined(_WIN32)
  /// Get a Windows style environment pointer.
  ///
  /// This pointer is only valid for the lifetime of the environment itself.
  /// CreateProcessW requires a mutable pointer, so we allocate and return a
  /// copy.
  std::unique_ptr<wchar_t[]> getWindowsEnvp() {
    isFrozen = true;

    // Form the final environment.
    // On Windows, the environment must be a contiguous null-terminated block
    // of null-terminated strings followed by an additional null terminator
    env.clear();
    for (const auto& entry : envStorage) {
      llvm::SmallVector<llvm::UTF16, 20> wEntry;
      llvm::convertUTF8ToUTF16String(entry, wEntry);
      env.insert(env.end(), wEntry.begin(), wEntry.end());
    }
    env.emplace_back(L'\0');
    auto envData = std::make_unique<wchar_t[]>(env.size());
    std::copy(env.begin(), env.end(), envData.get());
    return envData;
  };
#else
  /// Get a POSIX style envirnonment pointer.
  ///
  /// This pointer is only valid for the lifetime of the environment itself.
  const char* const* getEnvp() {
    isFrozen = true;

    // Form the final environment.
    env.clear();
    for (const auto& entry : envStorage) {
      env.emplace_back(entry.c_str());
    }
    env.emplace_back(nullptr);
    return env.data();
  }
#endif
};
}
}

#endif
