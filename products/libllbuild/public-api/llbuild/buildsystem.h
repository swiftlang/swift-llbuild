//===- buildsystem.h ----------------------------------------------*- C -*-===//
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
// These are the C API interfaces to the llbuild BuildSystem component.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_PUBLIC_BUILDSYSTEM_H
#define LLBUILD_PUBLIC_BUILDSYSTEM_H

#ifndef LLBUILD_PUBLIC_LLBUILD_H
#error Clients must include the "llbuild.h" umbrella header.
#endif

#include <stdbool.h>
#include <stdint.h>

/// @name Build System
/// @{

/// Opaque handle to a build system.
typedef struct llb_buildsystem_t_ llb_buildsystem_t;

/// Opaque handle to a build system command.
typedef struct llb_buildsystem_command_t_ llb_buildsystem_command_t;

/// Opaque handle to a build system command's launched process.
typedef struct llb_buildsystem_process_t_ llb_buildsystem_process_t;

/// Invocation parameters for a build system.
typedef struct llb_buildsystem_invocation_t_ llb_buildsystem_invocation_t;
struct llb_buildsystem_invocation_t_ {
    /// The path of the build file to use.
    const char* buildFilePath;
    
    /// The path of the database file to use, if any.
    const char* dbPath;

    /// The path of the build trace output file to use, if any.
    const char* traceFilePath;
    
    /// Whether to show verbose output.
    //
    // FIXME: This doesn't belong here, move once the status is fully delegated.
    bool showVerboseStatus;
    
    /// Whether to use a serial build.
    bool useSerialBuild;
};

/// Delegate structure for callbacks required by the build system.
typedef struct llb_buildsystem_delegate_t_ {
    /// User context pointer.
    void* context;

    void (*command_started)(void* context, llb_buildsystem_command_t* command);

    void (*command_finished)(void* context, llb_buildsystem_command_t* command);

    void (*command_process_started)(void* context,
                                    llb_buildsystem_command_t* command,
                                    llb_buildsystem_process_t* process);
    void (*command_process_had_output)(void* context,
                                       llb_buildsystem_command_t* command,
                                       llb_buildsystem_process_t* process,
                                       const llb_data_t* data);
    void (*command_process_finished)(void* context,
                                     llb_buildsystem_command_t* command,
                                     llb_buildsystem_process_t* process,
                                     int exit_status);
} llb_buildsystem_delegate_t;

/// Create a new build system instance.
LLBUILD_EXPORT llb_buildsystem_t*
llb_buildsystem_create(llb_buildsystem_delegate_t delegate,
                       llb_buildsystem_invocation_t invocation);

/// Destroy a build system invocation.
LLBUILD_EXPORT void
llb_buildsystem_destroy(llb_buildsystem_t* system);

/// Build the named target.
LLBUILD_EXPORT bool
llb_buildsystem_build(llb_buildsystem_t* system, const llb_data_t* key);

/// Get the name of the given command.
///
/// \param key_out - On return, contains a pointer to the name of the command.
LLBUILD_EXPORT void
llb_buildsystem_command_get_name(llb_buildsystem_command_t* command,
                                 llb_data_t* key_out);

/// @}

#endif
