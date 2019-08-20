//===- PlatformUtility.h ----------------------------------------*- C++ -*-===//
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
// This file implements small platform compatability wrapper functions for
// common functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_PLATFORMUTILITY_H
#define LLBUILD_BASIC_PLATFORMUTILITY_H

#include "llbuild/Basic/CrossPlatformCompatibility.h"
#include <cstdint>
#include <cstdio>
#include <string>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#endif
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace llbuild {
namespace basic {
namespace sys {

bool chdir(const char *fileName);
int close(int fileHandle);
bool mkdir(const char *fileName);
int pclose(FILE *stream);
int pipe(int ptHandles[2]);
FILE *popen(const char *command, const char *mode);
int read(int fileHandle, void *destinationBuffer, unsigned int maxCharCount);
int rmdir(const char *path);
int symlink(const char *source, const char *target);
int unlink(const char *fileName);
int write(int fileHandle, void *destinationBuffer, unsigned int maxCharCount);
std::string strerror(int error);
char *strsep(char **stringp, const char *delim);
// Create a directory in the temporary folder which doesn't exist and return
// it's path.
std::string makeTmpDir();

// Return a string containing all valid path separators on the current platform
std::string getPathSeparators();

/// Sets the max open file limit to min(max(soft_limit, limit), hard_limit),
/// where soft_limit and hard_limit are gathered from the system.
///
/// Returns: 0 on success, -1 on failure (check errno).
int raiseOpenFileLimit(llbuild_rlim_t limit = 2048);

enum MATCH_RESULT { MATCH, NO_MATCH, MATCH_ERROR };
// Test if a path or filename matches a wildcard pattern
//
// Returns MATCH if a match is detected, NO_MATCH if there is no match, and
// MATCH_ERROR on error. Windows callers may use GetLastError to get additional
// error information.
MATCH_RESULT filenameMatch(const std::string& pattern, const std::string& filename);

void sleep(int seconds);
template <typename = FD> struct FileDescriptorTraits;

#if defined(_WIN32)
template <> struct FileDescriptorTraits<HANDLE> {
  static bool IsValid(HANDLE hFile) { return hFile != INVALID_HANDLE_VALUE; }
  static void Close(HANDLE hFile) { CloseHandle(hFile); }
  static int Read(HANDLE hFile, void *destinationBuffer,
                  unsigned int maxCharCount) {
    DWORD numBytes;
    if (!ReadFile(hFile, destinationBuffer, maxCharCount, &numBytes, nullptr)) {
      return -1;
    }
    return numBytes;
  }
};
#endif
template <> struct FileDescriptorTraits<int> {
  static bool IsValid(int fd) { return fd >= 0; }
  static void Close(int fd) { close(fd); }
  static int Read(int hFile, void *destinationBuffer,
                  unsigned int maxCharCount) {
    return read(hFile, destinationBuffer, maxCharCount);
  }
};

enum class OSStyle {
  Windows,
  POSIX,

#if defined(_WIN32)
  Default = Windows,
#elif defined(_POSIX_VERSION)
  Default = POSIX,
#endif
};

template <OSStyle = OSStyle::Default>
struct ModuleTraits;

#if defined(_WIN32)
template <>
struct ModuleTraits<OSStyle::Windows> {
  using Handle = HMODULE;
};
#endif

template <>
struct ModuleTraits<OSStyle::POSIX> {
  using Handle = void *;
};

ModuleTraits<>::Handle OpenLibrary(const char *);
void *GetSymbolByname(ModuleTraits<>::Handle, const char *);
void CloseLibrary(ModuleTraits<>::Handle);

}
}
}

#endif
