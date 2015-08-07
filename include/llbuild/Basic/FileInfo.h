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

#include <cstdint>
#include <string>

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

/// File information which is intended to be used as a proxy for when a file has
/// changed.
///
/// This structure is intentionally sized to have no packing holes.
struct FileInfo {
  uint64_t device;
  uint64_t inode;
  uint64_t size;
  FileTimestamp modTime;

  /// Check if this is a FileInfo representing a missing file.
  bool isMissing() const {
    // We use an all-zero FileInfo as a sentinel, under the assumption this can
    // never exist in normal circumstances.
    return (device == 0 && inode == 0 && size == 0 &&
            modTime.seconds == 0 && modTime.nanoseconds == 0);
  }

  bool operator==(const FileInfo& rhs) const {
    return (device == rhs.device &&
            inode == rhs.inode &&
            size == rhs.size &&
            modTime == rhs.modTime);
  }
  bool operator!=(const FileInfo& rhs) const {
    return !(*this == rhs);
  }

  /// Get the information to represent the state of the given node in the file
  /// system.
  ///
  /// \returns The FileInfo for the given path, which will be missing if the
  /// path does not exist (or any error was encountered).
  static FileInfo getInfoForPath(const std::string& path);
};

}
}

#endif
