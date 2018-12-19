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
}
}
}

#endif
