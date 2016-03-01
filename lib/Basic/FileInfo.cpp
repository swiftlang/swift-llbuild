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

#include <cassert>
#include <cstring>
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
  result.mode = buf.st_mode;
  result.size = buf.st_size;
#if defined(__APPLE__)
  auto seconds = buf.st_mtimespec.tv_sec;
  auto nanoseconds = buf.st_mtimespec.tv_nsec;
#else
  auto seconds = buf.st_mtim.tv_sec;
  auto nanoseconds = buf.st_mtim.tv_nsec;
#endif
  result.modTime.seconds = seconds;
  result.modTime.nanoseconds = nanoseconds;

  // Enforce we never accidentally create our sentinel missing file value.
  if (result.isMissing()) {
    result.modTime.nanoseconds = 1;
    assert(!result.isMissing());
  }

  return result;
}
