//===- llbuild.h --------------------------------------------------*- C -*-===//
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
// These are the C API interfaces to the llbuild library.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_PUBLIC_LLBUILD_H
#define LLBUILD_PUBLIC_LLBUILD_H

#include "llbuild-defines.h"

/// Get the full version of the llbuild library.
LLBUILD_EXPORT const char* llb_get_full_version_string(void);

/// Get the C API version number.
LLBUILD_EXPORT int llb_get_api_version(void);

// The Core component.
#include "core.h"

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
// The BuildSystem component.
#include "buildsystem.h"
#endif

// The Database component.
#include "db.h"

#include "buildkey.h"
#include "buildvalue.h"

#include "ninja.h"

#endif
