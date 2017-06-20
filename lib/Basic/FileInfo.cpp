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

#include "llbuild/Basic/Stat.h"

#include <cassert>
#include <cstring>

using namespace llbuild;
using namespace llbuild::basic;

bool FileInfo::isDirectory() const {
  return (mode & S_IFDIR) != 0;
}

/// Get the information to represent the state of the given node in the file
/// system.
///
/// \param info_out [out] On success, the important path information.
/// \returns True if information on the path was found.
FileInfo FileInfo::getInfoForPath(const std::string& path, bool asLink) {
  FileInfo result;

  sys::StatStruct buf;
  auto statResult =
    asLink ? sys::lstat(path.c_str(), &buf) : sys::stat(path.c_str(), &buf);
  if (statResult != 0) {
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
#elif defined(_WIN32)
  auto seconds = buf.st_mtime;
  auto nanoseconds = 0;
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
