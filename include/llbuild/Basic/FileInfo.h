//===- FileInfo.h -----------------------------------------------*- C++ -*-===//
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
//
// This file contains the FileInfo wrapper which is shared by the Ninja and
// BuildSystem libraries.
//
// FIXME: I am ambivalent about this living in Basic, I want all non-functional
// pieces to generally be pretty isolated (and ideally always mediated by a
// delegate access). We may eventually want a specific FileSystem component for
// dealing with efficient and abstracted access to the file system and
// containing other pieces (like stat caching, or dealing with distribution or
// virtualization).
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_FILEINFO_H
#define LLBUILD_BASIC_FILEINFO_H

#include "BinaryCoding.h"

#include <cstdint>
#include <string>
#include "llvm/Support/FileSystem.h"

namespace llbuild {
namespace basic {

/// File timestamp wrapper.
struct FileTimestamp {
  uint64_t seconds;
  uint64_t nanoseconds;
  
  bool operator==(const FileTimestamp& rhs) const {
    return seconds == rhs.seconds && nanoseconds == rhs.nanoseconds;
  }
  bool operator!=(const FileTimestamp& rhs) const {
    return !(*this == rhs);
  }
  bool operator<(const FileTimestamp& rhs) const {
    return (seconds < rhs.seconds ||
            (seconds == rhs.seconds && nanoseconds < rhs.nanoseconds));
  }
  bool operator<=(const FileTimestamp& rhs) const {
    return (seconds < rhs.seconds ||
            (seconds == rhs.seconds && nanoseconds <= rhs.nanoseconds));
  }
  bool operator>(const FileTimestamp& rhs) const {
    return rhs < *this;
  }
  bool operator>=(const FileTimestamp& rhs) const {
    return rhs <= *this;
  }
};

struct LazyMD5Digest {
  std::string path = "";
  mutable std::pair<uint64_t, uint64_t> digest;
  mutable bool needsCalculation;
  
  bool operator==(const LazyMD5Digest& rhs) const {
    ensureCalculated();
    rhs.ensureCalculated();
    return digest == rhs.digest;
  }
  bool operator!=(const LazyMD5Digest& rhs) const {
    return !(*this == rhs);
  }

  void ensureCalculated() const {
    if (needsCalculation && !path.empty()) {
      digest = llvm::sys::fs::md5_contents(Twine(path))->words();
      needsCalculation = false;
    }
  }
};

/// File information which is intended to be used as a proxy for when a file has
/// changed.
///
/// This structure is intentionally sized to have no packing holes.
struct FileInfo {
  /// The device number.
  uint64_t device;
  /// The inode number.
  uint64_t inode;
  /// The mode flags of the file.
  uint64_t mode;
  /// The size of the file.
  uint64_t size;
  /// The modification time of the file.
  FileTimestamp modTime;
  /// The MD5 hash of the file.
  LazyMD5Digest digest;

  /// Check if this is a FileInfo representing a missing file.
  bool isMissing() const {
    // We use an all-zero FileInfo as a sentinel, under the assumption this can
    // never exist in normal circumstances.
    return (device == 0 && inode == 0 && mode == 0 && size == 0 &&
            modTime.seconds == 0 && modTime.nanoseconds == 0);
  }

  /// Check if the FileInfo corresponds to a directory.
  bool isDirectory() const;
  
  bool operator==(const FileInfo& rhs) const {
    return (size == rhs.size &&
            (modTime == rhs.modTime || digest == rhs.digest));
  }
  bool operator!=(const FileInfo& rhs) const {
    return !(*this == rhs);
  }

  /// Get the information to represent the state of the given node in the file
  /// system.
  ///
  /// \param asLink If yes, checks the information for the file path without
  /// looking through symbolic links.
  ///
  /// \returns The FileInfo for the given path, which will be missing if the
  /// path does not exist (or any error was encountered).
  static FileInfo getInfoForPath(const std::string& path, bool asLink = false);
};

template<>
struct BinaryCodingTraits<FileTimestamp> {
  static inline void encode(const FileTimestamp& value, BinaryEncoder& coder) {
    coder.write(value.seconds);
    coder.write(value.nanoseconds);
  }
  static inline void decode(FileTimestamp& value, BinaryDecoder& coder) {
    coder.read(value.seconds);
    coder.read(value.nanoseconds);
  }
};

template<>
struct BinaryCodingTraits<LazyMD5Digest> {
  static inline void encode(const LazyMD5Digest& value, BinaryEncoder& coder) {
    value.ensureCalculated();
    coder.write(value.digest.first);
    coder.write(value.digest.second);
  }
  static inline void decode(LazyMD5Digest& value, BinaryDecoder& coder) {
    coder.read(value.digest.first);
    coder.read(value.digest.second);
  }
};

template<>
struct BinaryCodingTraits<FileInfo> {
  static inline void encode(const FileInfo& value, BinaryEncoder& coder) {
    coder.write(value.device);
    coder.write(value.inode);
    coder.write(value.mode);
    coder.write(value.size);
    coder.write(value.modTime);
    coder.write(value.digest);
  }
  static inline void decode(FileInfo& value, BinaryDecoder& coder) {
    coder.read(value.device);
    coder.read(value.inode);
    coder.read(value.mode);
    coder.read(value.size);
    coder.read(value.modTime);
    coder.read(value.digest);
  }
};

}
}

#endif
