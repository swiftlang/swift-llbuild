//===-- FileSystem.cpp ----------------------------------------------------===//
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

#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Basic/Stat.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/MemoryBuffer.h"

#include <cassert>
#include <cstring>

// Cribbed from llvm, where it's been since removed.
namespace {
  using namespace std;
  using namespace llvm;
  using namespace llvm::sys::fs;

  static std::error_code fillStatus(int StatRet, const llbuild::basic::sys::StatStruct &Status,
                                    file_status &Result) {
    if (StatRet != 0) {
      std::error_code ec(errno, std::generic_category());
      if (ec == errc::no_such_file_or_directory)
        Result = file_status(file_type::file_not_found);
      else
        Result = file_status(file_type::status_error);
      return ec;
    }

    file_type Type = file_type::type_unknown;

    if (S_ISDIR(Status.st_mode))
      Type = file_type::directory_file;
    else if (S_ISREG(Status.st_mode))
      Type = file_type::regular_file;
    else if (S_ISBLK(Status.st_mode))
      Type = file_type::block_file;
    else if (S_ISCHR(Status.st_mode))
      Type = file_type::character_file;
    else if (S_ISFIFO(Status.st_mode))
      Type = file_type::fifo_file;
    else if (S_ISSOCK(Status.st_mode))
      Type = file_type::socket_file;
    else if (S_ISLNK(Status.st_mode))
      Type = file_type::symlink_file;

#if defined(_WIN32)
    Result = file_status(Type);
#else
    perms Perms = static_cast<perms>(Status.st_mode);
    Result =
    file_status(Type, Perms, Status.st_dev, Status.st_ino, Status.st_mtime,
                Status.st_uid, Status.st_gid, Status.st_size);
#endif

    return std::error_code();
  }

  std::error_code link_status(const Twine &Path, file_status &Result) {
    SmallString<128> PathStorage;
    StringRef P = Path.toNullTerminatedStringRef(PathStorage);

    llbuild::basic::sys::StatStruct Status;
    int StatRet = llbuild::basic::sys::lstat(P.begin(), &Status);
    return fillStatus(StatRet, Status, Result);
  }

  error_code _remove_all_r(StringRef path, file_type ft, uint32_t &count) {
    if (ft == file_type::directory_file) {
      error_code ec;
      directory_iterator i(path, ec);
      if (ec)
        return ec;

      for (directory_iterator e; i != e; i.increment(ec)) {
        if (ec)
          return ec;

        file_status st;

        if (error_code ec = link_status(i->path(), st))
          return ec;

        if (error_code ec = _remove_all_r(i->path(), st.type(), count))
          return ec;
      }

      if (error_code ec = remove(path, false))
        return ec;

      ++count; // Include the directory itself in the items removed.
    } else {
      if (error_code ec = remove(path, false))
        return ec;

      ++count;
    }

    return error_code();
  }
}

using namespace llbuild;
using namespace llbuild::basic;

FileSystem::~FileSystem() {}

bool FileSystem::createDirectories(const std::string& path) {
  // Attempt to create the final directory first, to optimize for the common
  // case where we don't need to recurse.
  if (createDirectory(path))
    return true;

  // If that failed, attempt to create the parent.
  StringRef parent = llvm::sys::path::parent_path(path);
  if (parent.empty())
    return false;
  return createDirectories(parent) && createDirectory(path);
}

namespace {

class LocalFileSystem : public FileSystem {
public:
  LocalFileSystem() {}

  virtual bool
  createDirectory(const std::string& path) override {
    if (!llbuild::basic::sys::mkdir(path.c_str())) {
      if (errno != EEXIST) {
        return false;
      }
    }
    return true;
  }

  virtual std::unique_ptr<llvm::MemoryBuffer>
  getFileContents(const std::string& path) override {
    auto result = llvm::MemoryBuffer::getFile(path);
    if (result.getError()) {
      return nullptr;
    }
    return std::unique_ptr<llvm::MemoryBuffer>(result->release());
  }

  bool rm_tree(const char* path) {
    uint32_t count = 0;
    return !_remove_all_r(path, file_type::directory_file, count);
  }

  virtual bool remove(const std::string& path) override {
    // Assume `path` is a regular file.
    if (llbuild::basic::sys::unlink(path.c_str()) == 0) {
      return true;
    }

    // Error can't be that `path` is actually a directory (on Linux `EISDIR` will be returned since 2.1.132).
    if (errno != EPERM && errno != EISDIR) {
      return false;
    }

    // Check if `path` is a directory.
    llbuild::basic::sys::StatStruct statbuf;
    if (llbuild::basic::sys::lstat(path.c_str(), &statbuf) != 0) {
      return false;
    }

    if (S_ISDIR(statbuf.st_mode)) {
      if (llbuild::basic::sys::rmdir(path.c_str()) == 0) {
        return true;
      } else {
        return rm_tree(path.c_str());
      }
    }

    return false;
  }
  
  virtual FileInfo getFileInfo(const std::string& path) override {
    return FileInfo::getInfoForPath(path);
  }
  
  virtual FileInfo getLinkInfo(const std::string& path) override {
    return FileInfo::getInfoForPath(path, /*isLink:*/ true);
  }
};
  
}

std::unique_ptr<FileSystem> basic::createLocalFileSystem() {
  return llvm::make_unique<LocalFileSystem>();
}
