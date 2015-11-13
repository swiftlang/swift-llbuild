//===- libstdc++14-workaround.h ---------------------------------*- C++ -*-===//
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

#if defined(__cplusplus)

/* Workaround broken libstdc++ issue with C++14, see:
 *   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=51785
 *
 * We work around this by forcing basically everything to include libstdc++'s config.
 */
#include <initializer_list>
#undef _GLIBCXX_HAVE_GETS

#endif
