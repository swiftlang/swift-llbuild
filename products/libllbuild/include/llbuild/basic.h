//===- basic.h ----------------------------------------------*- C -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// These are the C API interfaces to the llbuild BuildSystem component.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_PUBLIC_BASIC_H
#define LLBUILD_PUBLIC_BASIC_H

#ifndef LLBUILD_PUBLIC_LLBUILD_H
#error Clients must include the "llbuild.h" umbrella header.
#endif

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#if !defined(__APPLE__) || !TARGET_OS_IPHONE

#include <stdbool.h>
#include <stdint.h>

#ifdef _MSC_VER
// Ignore the conflicting min/max defined in windows.h
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE llbuild_pid_t;
typedef HANDLE FD;
#else
#if defined(__linux__) || defined(__GNU__)
#include <termios.h>
#else
#include <sys/types.h>
#endif // defined(__linux__) || defined(__GNU__)
#include <unistd.h>
typedef pid_t llbuild_pid_t;
typedef int FD;
#endif // _MSC_VER

/// @name File System Behaviors
/// @{

/// A fine-grained timestamp.
///
/// Although phrased as a time, this value is uninterpreted by llbuild. The
/// client may represent time in any fashion intended to preserve uniqueness.
typedef struct llb_fs_timestamp_t_ {
    uint64_t seconds;
    uint64_t nanoseconds;
} llb_fs_timestamp_t;

/// Information on the status of a file.
typedef struct llb_fs_file_info_t_ {
  /// A unique identifier for the device containing the file.
  uint64_t device;

  /// A unique identifier for the file on the device.
  uint64_t inode;

  /// The file mode information, as used by stat(2).
  uint64_t mode;

  /// The size of the file.
  uint64_t size;

  /// An indication of the last modification time.
  llb_fs_timestamp_t mod_time;
} llb_fs_file_info_t;

/// @}

/// Scheduler algorithms
typedef enum LLBUILD_ENUM_ATTRIBUTES {
  /// Command name priority queue based scheduling [default]
  llb_scheduler_algorithm_command_name_priority LLBUILD_SWIFT_NAME(commandNamePriority) = 0,

  /// First in, first out
  llb_scheduler_algorithm_fifo = 1
} llb_scheduler_algorithm_t LLBUILD_SWIFT_NAME(SchedulerAlgorithm);


// MARK: Quality of Service

/// Quality of service levels.
typedef enum LLBUILD_ENUM_ATTRIBUTES {
    /// A default quality of service (i.e. what the system would use without
    /// other advisement, generally this would be comparable to what would be
    /// done by `make`, `ninja`, etc.)
    llb_quality_of_service_default = 0,

    /// User-initiated, high priority work.
    llb_quality_of_service_user_initiated LLBUILD_SWIFT_NAME(userInitiated) = 1,

    /// Batch work performed on behalf of the user.
    llb_quality_of_service_utility = 2,

    /// Background work that is not directly visible to the user.
    llb_quality_of_service_background = 3
} llb_quality_of_service_t LLBUILD_SWIFT_NAME(QualityOfService);

/// Get the global quality of service level to use for processing.
LLBUILD_EXPORT llb_quality_of_service_t
llb_get_quality_of_service();

/// Set the global quality of service level to use for processing.
LLBUILD_EXPORT void
llb_set_quality_of_service(llb_quality_of_service_t level);

// MARK: Execution Queue Scheduler Control

/// Get the global scheduler algorithm setting.
LLBUILD_EXPORT llb_scheduler_algorithm_t
llb_get_scheduler_algorithm();

/// Set the global scheduler algorthm used for the execution queue. This will
/// only take effect when constructing a new execution queue (i.e. for a build
/// operation).
LLBUILD_EXPORT void
llb_set_scheduler_algorithm(llb_scheduler_algorithm_t algorithm);

/// Get the global scheduler lane width setting.
LLBUILD_EXPORT uint32_t
llb_get_scheduler_lane_width();

/// Set the global scheduler lane width. This will only take effect when
/// constructing a new execution queue (i.e. for a build operation).
///
/// \param width The number of lanes to schedule. A value of 0 [default] will
/// be automatically translated into the number of cores detected on the host.
LLBUILD_EXPORT void
llb_set_scheduler_lane_width(uint32_t width);
/// @}

/// @name Memory APIs
// MARK: Allocating and freeing memory
/// Allocate memory usable by the build system
/// \param size The number bytes to allocate
LLBUILD_EXPORT void*
llb_alloc(size_t size);
/// Free memory allocated for or by the build system
/// \param ptr A pointer to the allocated memory to free
LLBUILD_EXPORT void
llb_free(void* ptr);
/// @}

#endif

#endif
