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

#ifndef TRITIUM_VISIBILITY_H
#define TRITIUM_VISIBILITY_H

#if defined(__ELF__) || (defined(__APPLE__) && defined(__MACH__))
#define TRITIUM_EXPORT __attribute__((__visibility__("default")))
#else
// asume PE/COFF
#if defined(_WINDLL)
#define TRITIUM_EXPORT __declspec(dllexport)
#else
#define TRITIUM_EXPORT
#endif
#endif

#endif
