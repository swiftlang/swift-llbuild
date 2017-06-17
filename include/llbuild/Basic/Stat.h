//===- Stat.h -------------------------------------------------------------===//
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

#ifndef LLBUILD_BASIC_STAT_H
#define LLBUILD_BASIC_STAT_H

#include <sys/stat.h>

#include "llvm/Support/FileSystem.h"

namespace llbuild {
namespace basic {
namespace sys {

#if !defined(S_IFBLK)
#define S_IFBLK 0060000
#endif
#if !defined(S_IFIFO)
#define S_IFIFO 0010000
#endif
#if !defined(S_IFSOCK)
#define S_IFSOCK 00140000
#endif
#if !defined(S_IFLNK)
#define S_IFLNK 0120000
#endif

#if !defined(S_ISREG)
#define S_ISREG(mode) ((mode) & _S_IFMT) == S_IFREG
#endif

#if !defined(S_ISDIR)
#define S_ISDIR(mode) ((mode) & _S_IFMT) == S_IFDIR
#endif

#if !defined(S_ISBLK)
#define S_ISBLK(mode) ((mode) & _S_IFMT) == S_IFBLK
#endif

#if !defined(S_ISCHR)
#define S_ISCHR(mode) ((mode) & _S_IFMT) == S_IFCHR
#endif

#if !defined(S_ISFIFO)
#define S_ISFIFO(mode) ((mode) & _S_IFMT) == S_IFIFO
#endif

#if !defined(S_ISSOCK)
#define S_ISSOCK(mode) ((mode) & _S_IFMT) == S_IFSOCK
#endif

#if !defined(S_ISLNK)
#define S_ISLNK(mode) ((mode) & _S_IFMT) == S_IFLNK
#endif

#if defined(_WIN32)
using StatStruct = struct ::_stat;
#else
using StatStruct = struct ::stat;
#endif

int lstat(const char *fileName, StatStruct *buf);
int stat(const char *fileName, StatStruct *buf);
}
}
}

#endif // LLBUILD_BASIC_STAT_H
