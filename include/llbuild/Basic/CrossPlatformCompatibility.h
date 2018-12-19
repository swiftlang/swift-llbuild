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
// This file defines cross platform definitions.
//
//===----------------------------------------------------------------------===//

#ifndef CrossPlatformCompatibility_h
#define CrossPlatformCompatibility_h

#if defined(_WIN32)
// Ignore the conflicting min/max defined in windows.h
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#if defined(__linux__) || defined(__GNU__)
#include <termios.h>
#else
#include <sys/types.h>
#endif // defined(__linux__) || defined(__GNU__)
#endif // _WIN32

#if defined(_WIN32)
typedef HANDLE FD;
#else
typedef int FD;
#endif

#if defined(_WIN32)
typedef HANDLE llbuild_pid_t;
#else
typedef pid_t llbuild_pid_t;
#endif

#if defined(_WIN32)
typedef int llbuild_rlim_t;
#else
typedef rlim_t llbuild_rlim_t;
#endif

template <typename = FD> struct FileDescriptorTraits;

#if defined(_WIN32)
template <> struct FileDescriptorTraits<HANDLE> {
  static void Close(HANDLE hFile) { CloseHandle(hFile); }
};
#else
template <> struct FileDescriptorTraits<int> {
  static void Close(int fd) { close(fd); }
};
#endif

#endif /* CrossPlatformCompatibility_h */
