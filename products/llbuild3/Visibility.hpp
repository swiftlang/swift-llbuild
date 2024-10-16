//===- Visibility.hpp -------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_VISIBILITY_H
#define LLBUILD3_VISIBILITY_H

#if defined(__ELF__) || (defined(__APPLE__) && defined(__MACH__))
#define LLBUILD3_EXPORT __attribute__((__visibility__("default")))
#else
// asume PE/COFF
#if defined(_WINDLL)
#define LLBUILD3_EXPORT __declspec(dllexport)
#else
#define LLBUILD3_EXPORT
#endif
#endif

#endif
