//===- buildsystem.h ----------------------------------------------*- C -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2016 Apple Inc. and the Swift project authors
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

/// @name Diagnostics
/// @{

typedef struct llb_fs_file_info_t_ {
  /// A unique identifier for the device containing the file.
  uint64_t device;

  /// A unique identifier for the file on the device.
  uint64_t inode;

  /// The size of the file.
  uint64_t size;

  /// An indication of the last modification time.
  ///
  /// Although phrased as a time, this value is uninterpreted by llbuild. The
  /// client may represent time in any fashion intended to preserve uniqueness.
  struct {
    uint64_t seconds;
    uint64_t nanoseconds;
  } mod_time;
} llb_fs_file_info_t;

/// @}

/// @name Diagnostics
/// @{

typedef enum {
  llb_buildsystem_diagnostic_kind_note,
  llb_buildsystem_diagnostic_kind_warning,
  llb_buildsystem_diagnostic_kind_error,
} llb_buildsystem_diagnostic_kind_t;

/// Get the name of the diagnostic kind.
LLBUILD_EXPORT const char*
llb_buildsystem_diagnostic_kind_get_name(
    llb_buildsystem_diagnostic_kind_t kind);

/// @}

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

  /// @name FileSystem APIs
  ///
  /// These are optional callbacks, which can be provided by the client to
  /// support virtualization or testing of the system. If not defined, the build
  /// system will directly access the local disk for file operations.
  ///
  /// @{

  /// Get the file contents for the given path.
  ///
  /// The contents *MUST* be returned in a new buffer allocated with \see
  /// malloc().
  ///
  /// Xparam path The path to provide the contents for.
  ///
  /// Xparam data_out On success, this should be filled in with a pointer to
  /// the newly created buffer containing the contents, and with the contents
  /// size.
  ///
  /// \\returns True on success.
  //
  //
  // FIXME: Design clean data types for clients to return unowned/unowned
  // memory.
  bool (*fs_get_file_contents)(void* context, const char* path,
                               llb_data_t* data_out);

  /// Get the file information for the given path.
  void (*fs_get_file_info)(void* context, const char* path,
                           llb_fs_file_info_t* data_out);
  
  /// Called to report an unassociated diagnostic from the build system.
  ///
  /// Xparam kind The kind of diagnostic.
  /// Xparam filename The filename associated with the diagnostic, if any.
  /// Xparam lin The line number associated with the diagnostic, if any.
  /// Xparam column The line number associated with the diagnostic, if any.
  /// Xparam message The diagnostic message, as a C string.
  void (*handle_diagnostic)(void* context,
                            llb_buildsystem_diagnostic_kind_t kind,
                            const char* filename, int line, int column,
                            const char* message);
                       
  /// Called when a command's job has been started.
  ///
  /// The system guarantees that any commandStart() call will be paired with
  /// exactly one \see commandFinished() call.
  void (*command_started)(void* context, llb_buildsystem_command_t* command);

  /// Called when a command's job has been finished.
  void (*command_finished)(void* context, llb_buildsystem_command_t* command);

  /// Called when a command's job has started executing an external process.
  ///
  /// The system guarantees that any commandProcessStarted() call will be paired
  /// with exactly one \see commandProcessFinished() call.
  ///
  /// Xparam process A unique handle used in subsequent delegate calls to
  /// identify the process. This handle should only be used to associate
  /// different status calls relating to the same process. It is only guaranteed
  /// to be unique from when it has been provided here to when it has been
  /// provided to the \see commandProcessFinished() call.
  void (*command_process_started)(void* context,
                                  llb_buildsystem_command_t* command,
                                  llb_buildsystem_process_t* process);

  /// Called to report an error in the management of a command process.
  ///
  /// Xparam process The process handle.
  /// Xparam data The error message.
  void (*command_process_had_error)(void* context,
                                    llb_buildsystem_command_t* command,
                                    llb_buildsystem_process_t* process,
                                    const llb_data_t* data);
  
  /// Called to report a command processes' (merged) standard output and error.
  ///
  /// Xparam process The process handle.
  /// Xparam data The process output.
  void (*command_process_had_output)(void* context,
                                     llb_buildsystem_command_t* command,
                                     llb_buildsystem_process_t* process,
                                     const llb_data_t* data);
  
  /// Called when a command's job has finished executing an external process.
  ///
  /// Xparam process The handle used to identify the process. This handle
  /// will become invalid as soon as the client returns from this API call.
  ///
  /// Xparam exitStatus The exit status of the process.
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
/// \param key_out On return, contains a pointer to the name of the command.
LLBUILD_EXPORT void
llb_buildsystem_command_get_name(llb_buildsystem_command_t* command,
                                 llb_data_t* key_out);

/// Get the description for the given command.
///
/// \returns The command description, as a new C string. The client is
/// resonpsible for calling \see free() on the result.
//
// FIXME: This API most likely doesn't belong.
LLBUILD_EXPORT char*
llb_buildsystem_command_get_description(llb_buildsystem_command_t* command);

/// Get the verbose description for the given command.
///
/// \returns The verbose command description, as a new C string. The client is
/// resonpsible for calling \see free() on the result.
//
// FIXME: This API most likely doesn't belong.
LLBUILD_EXPORT char*
llb_buildsystem_command_get_verbose_description(
    llb_buildsystem_command_t* command);

/// @}

#endif
