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

#ifdef _MSC_VER
typedef int llbuild_pid_t;
#else

#if defined(__linux__) || defined(__GNU__)
#include <termios.h>
#else
#include <sys/types.h>
#endif

typedef pid_t llbuild_pid_t;
#endif

#endif /* CrossPlatformCompatibility_h */
