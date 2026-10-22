//===- Compiler.h -----------------------------------------------*- C++ -*-===//
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
// Compiler support and compatibility macros. Liberally taken from LLVM.
//
//===----------------------------------------------------------------------===//


#ifndef LLBUILD_BASIC_COMPILER_H
#define LLBUILD_BASIC_COMPILER_H

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

/// \macro LLBUILD_MSC_PREREQ
/// \brief Is the compiler MSVC of at least the specified version?
/// The common \param version values to check for are:
///  * 1700: Microsoft Visual Studio 2012 / 11.0
///  * 1800: Microsoft Visual Studio 2013 / 12.0
#ifdef _MSC_VER
#define LLBUILD_MSC_PREREQ(version) (_MSC_VER >= (version))

// We require at least MSVC 2012.
#if !LLBUILD_MSC_PREREQ(1700)
#error LLBUILD requires at least MSVC 2012.
#endif

#else
#define LLBUILD_MSC_PREREQ(version) 0
#endif

/// LLBUILD_DELETED_FUNCTION - Expands to = delete if the compiler supports it.
/// Use to mark functions as uncallable. Member functions with this should be
/// declared private so that some behavior is kept in C++03 mode.
///
/// class DontCopy {
/// private:
///   DontCopy(const DontCopy&) LLBUILD_DELETED_FUNCTION;
///   DontCopy &operator =(const DontCopy&) LLBUILD_DELETED_FUNCTION;
/// public:
///   ...
/// };
#if __has_feature(cxx_deleted_functions) || \
    defined(__GXX_EXPERIMENTAL_CXX0X__) || LLBUILD_MSC_PREREQ(1800)
#define LLBUILD_DELETED_FUNCTION = delete
#else
#define LLBUILD_DELETED_FUNCTION
#endif

#endif
