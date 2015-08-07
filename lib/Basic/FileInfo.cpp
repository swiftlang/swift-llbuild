//===-- FileInfo.cpp ------------------------------------------------------===//
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

#include "llbuild/Basic/FileInfo.h"

#include <sys/stat.h>

using namespace llbuild;
using namespace llbuild::basic;

/// Get the information to represent the state of the given node in the file
/// system.
///
/// \param info_out [out] On success, the important path information.
/// \returns True if information on the path was found.
FileInfo FileInfo::getInfoForPath(const std::string& path) {
  FileInfo result;
  struct ::stat buf;
  if (::stat(path.c_str(), &buf) != 0) {
    memset(&result, 0, sizeof(result));
    assert(result.isMissing());
    return result;
  }

  result.device = buf.st_dev;
  result.inode = buf.st_ino;
  result.size = buf.st_size;
  result.modTime.seconds = buf.st_mtimespec.tv_sec;
  result.modTime.nanoseconds = buf.st_mtimespec.tv_nsec;

  // Verify we didn't truncate any values.
  assert(result.device == (unsigned)buf.st_dev &&
         result.inode == (unsigned)buf.st_ino &&
         result.size == (unsigned)buf.st_size &&
         result.modTime.seconds == (unsigned)buf.st_mtimespec.tv_sec &&
         result.modTime.nanoseconds == (unsigned)buf.st_mtimespec.tv_nsec);

  // Enforce we never accidentally create our sentinel missing file value.
  if (result.isMissing()) {
    result.modTime.nanoseconds = 1;
    assert(!result.isMissing());
  }

  return result;
}
