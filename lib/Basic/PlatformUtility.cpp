//===- Support/PlatformUtility.cpp - Platform Specific Utilities ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Basic/Stat.h"

#if defined(_WIN32)
#include "LeanWindows.h"
#include <direct.h>
#include <io.h>
#else
#include <stdio.h>
#include <unistd.h>
#endif

using namespace llbuild;
using namespace llbuild::basic;

bool sys::chdir(const char *fileName) {
#if defined(_WIN32)
  return SetCurrentDirectoryA(fileName);
#else
  return ::chdir(fileName) == 0;
#endif
}

int sys::close(int fileHandle) {
#if defined(_WIN32)
  return ::_close(fileHandle);
#else
  return ::close(fileHandle);
#endif
}

int sys::lstat(const char *fileName, sys::StatStruct *buf) {
#if defined(_WIN32)
  // We deliberately ignore lstat on Windows, and delegate
  // to stat.
  return ::_stat(fileName, buf);
#else
  return ::lstat(fileName, buf);
#endif
}

bool sys::mkdir(const char* fileName) {
#if defined(_WIN32)
  return _mkdir(fileName) == 0;
#else
  return ::mkdir(fileName, S_IRWXU | S_IRWXG |  S_IRWXO) == 0;
#endif
}

int sys::pclose(FILE *stream) {
#if defined(_WIN32)
  return ::_pclose(stream);
#else
  return ::pclose(stream);
#endif
}

int sys::pipe(int ptHandles[2]) {
#if defined(_WIN32)
  return ::_pipe(ptHandles, 0, 0);
#else
  return ::pipe(ptHandles);
#endif
}

FILE *sys::popen(const char *command, const char *mode) {
#if defined(_WIN32)
  return ::_popen(command, mode);
#else
  return ::popen(command, mode);
#endif
}

int sys::read(int fileHandle, void *destinationBuffer,
  unsigned int maxCharCount) {
#if defined(_WIN32)
  return ::_read(fileHandle, destinationBuffer, maxCharCount);
#else
  return ::read(fileHandle, destinationBuffer, maxCharCount);
#endif
}

int sys::rmdir(const char *path) {
#if defined(_WIN32)
  return ::_rmdir(path);
#else
  return ::rmdir(path);
#endif
}

int sys::stat(const char *fileName, StatStruct *buf) {
#if defined(_WIN32)
  return ::_stat(fileName, buf);
#else
  return ::stat(fileName, buf);
#endif
}

int sys::symlink(const char *source, const char *target) {
#if defined(_WIN32)
  DWORD attributes = GetFileAttributesA(source);
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return ::CreateSymbolicLinkA(source, target, SYMBOLIC_LINK_FLAG_DIRECTORY);
  }

  return ::CreateSymbolicLinkA(source, target, 0);
#else
  return ::symlink(source, target);
#endif
}

int sys::unlink(const char *fileName) {
#if defined(_WIN32)
  return ::_unlink(fileName);
#else
  return ::unlink(fileName);
#endif
}

int sys::write(int fileHandle, void *destinationBuffer,
  unsigned int maxCharCount) {
#if defined(_WIN32)
  return ::_write(fileHandle, destinationBuffer, maxCharCount);
#else
  return ::write(fileHandle, destinationBuffer, maxCharCount);
#endif
}
