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

#include <cstdio>
#include <sys/stat.h>

namespace llbuild {
namespace basic {
namespace sys {
#if defined(_WIN32)
using StatStruct = struct ::_stat;
#else
using StatStruct = struct ::stat;
#endif

bool chdir(const char *fileName);
int close(int fileHandle);
int lstat(const char *fileName, StatStruct *buf);
int pclose(FILE *stream);
int pipe(int ptHandles[2]);
FILE *popen(const char *command, const char *mode);
int read(int fileHandle, void *destinationBuffer, unsigned int maxCharCount);
int stat(const char *fileName, StatStruct *buf);
int unlink(const char *fileName);
int write(int fileHandle, void *destinationBuffer, unsigned int maxCharCount);
}
}
}

#endif
