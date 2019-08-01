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

#if defined(__cplusplus)
#if defined(_WIN32)
#define LLBUILD_EXPORT extern "C" __declspec(dllexport)
#else
#define LLBUILD_EXPORT extern "C" __attribute__((visibility("default")))
#endif
#elif __GNUC__
#define LLBUILD_EXPORT extern __attribute__((visibility("default")))
#elif defined(_WIN32)
#define LLBUILD_EXPORT extern __declspec(dllexport)
#else
#define LLBUILD_EXPORT extern
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if __has_attribute(swift_name)
# define LLBUILD_SWIFT_NAME(_name) __attribute__((swift_name(#_name)))
#else
# define LLBUILD_SWIFT_NAME(_name)
#endif

#if __has_attribute(enum_extensibility)
#define LLBUILD_ENUM_ATTRIBUTES __attribute__((enum_extensibility(open)))
#else
#define LLBUILD_ENUM_ATTRIBUTES
#endif

#ifndef __has_feature
# define __has_feature(x) 0
#endif
#if !__has_feature(nullability)
# ifndef _Nullable
#  define _Nullable
# endif
# ifndef _Nonnull
#  define _Nonnull
# endif
#endif

#if __has_feature(assume_nonnull)
#define LLBUILD_ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define LLBUILD_ASSUME_NONNULL_END   _Pragma("clang assume_nonnull end")
#else
#define LLBUILD_ASSUME_NONNULL_BEGIN
#define LLBUILD_ASSUME_NONNULL_END
#endif

/// A monotonically increasing indicator of the llbuild API version.
///
/// The llbuild API is *not* stable. This value allows clients to conditionally
/// compile for multiple versions of the API.
///
/// Version History:
///
/// 8: Move scheduler algorithm and lanes into llb_buildsystem_invocation_t
///
/// 7: Added destroy_context task delegate method.
///
/// 6: Added delegate methods for specific diagnostics.
///
/// 5: Added `llb_buildsystem_command_extended_result_t`, changed command_process_finished signature.
///
/// 4: Added llb_buildsystem_build_node.
///
/// 3: Added command_had_error, command_had_note and command_had_warning delegate methods.
///
/// 2: Added `llb_buildsystem_command_result_t` parameter to command_finished.
///
/// 1: Added `environment` parameter to llb_buildsystem_invocation_t.
///
/// 0: Pre-history
#define LLBUILD_C_API_VERSION 8

/// Get the full version of the llbuild library.
LLBUILD_EXPORT const char* llb_get_full_version_string(void);

/// Get the C API version number.
LLBUILD_EXPORT int llb_get_api_version(void);

// The Core component.
#include "core.h"

// The BuildSystem component.
#include "buildsystem.h"

// The Database component.
#include "db.h"

#endif
