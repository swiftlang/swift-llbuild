//===- LeanWindows.h --------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_LEANWINDOWS_H
#define LLBUILD_BASIC_LEANWINDOWS_H

#if !defined(_WIN32)
#error "LeanWindows.h should only be included on Windows."
#endif

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#endif
