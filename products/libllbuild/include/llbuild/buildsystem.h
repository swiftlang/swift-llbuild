//===- buildsystem.h ----------------------------------------------*- C -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2017 Apple Inc. and the Swift project authors
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

/// @name Build System APIs
/// @{

/// Opaque handle to a build system.
typedef struct llb_buildsystem_t_ llb_buildsystem_t;

/// Opaque handle to a build system command.
typedef struct llb_buildsystem_command_t_ llb_buildsystem_command_t;

/// Opaque handle to a build system tool.
typedef struct llb_buildsystem_tool_t_ llb_buildsystem_tool_t;

/// Opaque handle to a build system command's launched process.
typedef struct llb_buildsystem_process_t_ llb_buildsystem_process_t;

/// Result of a command execution
typedef enum {
  llb_buildsystem_command_result_succeeded = 0,
  llb_buildsystem_command_result_failed = 1,
  llb_buildsystem_command_result_cancelled = 2,
  llb_buildsystem_command_result_skipped = 3,
} llb_buildsystem_command_result_t;

/// Status change event kinds.
typedef enum {
  /// Indicates the command is being scanned to determine if it needs to run.
  ///
  /// This type will always precede the other types for any particular
  /// command. Once scanning begins for a command, it is guaranteed to be
  /// followed by either an "is-up-to-date" or "is-complete" event.
  llb_buildsystem_command_status_kind_is_scanning = 0,

  /// Indicates the command is up-to-date, and does not need to run.
  llb_buildsystem_command_status_kind_is_up_to_date = 1,

  /// Indicates the command was run, and is now complete.
  ///
  /// The actual preparation and start of the command are handled via other
  /// individual delegate callbacks, and not status change events.
  llb_buildsystem_command_status_kind_is_complete = 2,
} llb_buildsystem_command_status_kind_t;

typedef enum {
  /// A key used to identify a command.
  llb_build_key_kind_command = 0,

  /// A key used to identify a custom task.
  llb_build_key_kind_custom_task = 1,

  /// A key used to identify directory contents.
  llb_build_key_kind_directory_contents = 2,

  /// A key used to identify the signature of a complete directory tree.
  llb_build_key_kind_directory_tree_signature = 3,

  /// A key used to identify a node.
  llb_build_key_kind_node = 4,

  /// A key used to identify a target.
  llb_build_key_kind_target = 5,

  /// An invalid key kind.
  llb_build_key_kind_unknown = 6,

  /// A key used to identify the signature of a complete directory tree.
  llb_build_key_kind_directory_tree_structure_signature = 7,
} llb_build_key_kind_t;

/// The BuildKey encodes the key space used by the BuildSystem when using the
/// core BuildEngine.
typedef struct llb_build_key_t_ llb_build_key_t;
struct llb_build_key_t_ {
  /// The kind of key
  llb_build_key_kind_t kind;
  /// The actual key data
  const char* key;
} ;

/// Invocation parameters for a build system.
typedef struct llb_buildsystem_invocation_t_ llb_buildsystem_invocation_t;
struct llb_buildsystem_invocation_t_ {
  /// The path of the build file to use.
  const char* buildFilePath;
    
  /// The path of the database file to use, if any.
  const char* dbPath;

  /// The path of the build trace output file to use, if any.
  const char* traceFilePath;

  /// The base environment to use when executing subprocesses.
  ///
  /// The format is expected to match that of `::main()`, i.e. a null-terminated
  /// array of pointers to null terminated C strings.
  ///
  /// If empty, the environment of the calling process will be used.
  ///
  /// The environment is *not* copied by the build invocation, and must remain
  /// alive for the lifetime of any build using this invocation.
  const char* const* environment;
  
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

  /// Create the given directory if it does not exist.
  ///
  /// \\returns True on success (the directory was created, or already exists).
  bool (*fs_create_directory)(void* context, const char* path);
  
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

  /// Remove file or directory at given path.
  ///
  /// Xparam path The path to remove.
  ///
  /// \\returns True on success.
  bool (*fs_remove)(void* context, const char* path);

  /// Get the file information for the given path.
  void (*fs_get_file_info)(void* context, const char* path,
                           llb_fs_file_info_t* data_out);

  /// Get the file information for the given path, without looking through
  /// symbolic links.
  void (*fs_get_link_info)(void* context, const char* path,
                           llb_fs_file_info_t* data_out);

  /// @}

  /// @name Build System Behaviors
  /// @{  

  /// Called by the build system to get an instance of the custom tool for the
  /// given name.
  ///
  /// Xreturns If the tool is recognized, the client should return an instance
  /// of the tool created with \see llb_buildsystem_create_tool(). If the tool
  /// is not recognized, the client should return null.
  llb_buildsystem_tool_t* (*lookup_tool)(void* context, const llb_data_t* name);
  
  /// @}

  /// @name Diagnostic & Status APIs
  /// @{  

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

  /// Called to report any form of command failure.
  ///
  /// This can may be called to report the failure of a command which has
  /// executed, but may also be used to report the inability of a command to
  /// run. It is expected to be used by the client in making decisions with
  /// regard to cancelling the build.
  ///
  /// This callback is optional. If not provided, the build will automatically
  /// be cancelled after the first failure.
  //
  // FIXME: Clean this up, we should be able to come up with a better definition
  // & API organization.
  void (*had_command_failure)(void* context);

  /// Called to report a status change for a command.
  ///
  /// The possible status changes -- and their invariants with respect to other
  /// notifications -- are defined in \see
  /// llb_buildsystem_command_status_kind_t.
  void (*command_status_changed)(void* context,
                                 llb_buildsystem_command_t* command,
                                 llb_buildsystem_command_status_kind_t kind);

  /// Called when a command is preparing to run.
  ///
  /// The system guarantees that any command_preparing() call will be paired
  /// with exactly one \see command_finished() call.
  void (*command_preparing)(void* context, llb_buildsystem_command_t* command);

  /// Called by the build system to allow the delegate to skip a command without
  /// implicitly skipping its dependents.
  ///
  /// WARNING: Clients need to take special care when using this. Skipping
  /// commands without considering their dependencies or dependents can easily
  /// produce an inconsistent build.
  ///
  /// This method is called before the command starts, when the system has
  /// identified that it will eventually need to run (after all of its inputs
  /// have been satisfied).
  ///
  /// The system guarantees that all such calls will be paired with a
  /// corresponding \see commandFinished() call.
  bool (*should_command_start)(void* context,
                               llb_buildsystem_command_t* command);

  /// Called when a command has been started.
  ///
  /// The system guarantees that any commandStart() call will be paired with
  /// exactly one \see command_finished() call.
  void (*command_started)(void* context, llb_buildsystem_command_t* command);

  /// Called when a command has been finished.
  ///
  /// Xparam result The result of command (e.g. success, failure, etc).
  void (*command_finished)(void* context,
                           llb_buildsystem_command_t* command,
                           llb_buildsystem_command_result_t result);

  /// Called to report an error during the execution of a command.
  ///
  /// Xparam data The error message.
  void (*command_had_error)(void* context,
                            llb_buildsystem_command_t* command,
                            const llb_data_t* data);

  /// Called to report a note during the execution of a command.
  ///
  /// Xparam data The note message.
  void (*command_had_note)(void* context,
                           llb_buildsystem_command_t* command,
                           const llb_data_t* data);

  /// Called to report a warning during the execution of a command.
  ///
  /// Xparam data The warning message.
  void (*command_had_warning)(void* context,
                           llb_buildsystem_command_t* command,
                           const llb_data_t* data);

  /// Called when a command's job has started executing an external process.
  ///
  /// The system guarantees that any commandProcessStarted() call will be paired
  /// with exactly one \see command_process_finished() call.
  ///
  /// Xparam process A unique handle used in subsequent delegate calls to
  /// identify the process. This handle should only be used to associate
  /// different status calls relating to the same process. It is only guaranteed
  /// to be unique from when it has been provided here to when it has been
  /// provided to the \see command_process_finished() call.
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
  /// Xparam result Whether the process suceeded, failed or was cancelled.
  /// Xparam exitStatus The raw exit status of the process.
  void (*command_process_finished)(void* context,
                                   llb_buildsystem_command_t* command,
                                   llb_buildsystem_process_t* process,
                                   llb_buildsystem_command_result_t result,
                                   int exit_status);

  /// Called when a cycle is detected by the build engine and it cannot make
  /// forward progress.
  ///
  /// Xparam rules The ordered list of items comprising the cycle, starting from
  /// the node which was requested to build and ending with the first node in
  /// the cycle (i.e., the node participating in the cycle will appear twice).
  /// Xparam rule_count The number of rules comprising the cycle.
  void (*cycle_detected)(void* context, llb_build_key_t* rules, uint64_t rule_count);
  
  /// @}
} llb_buildsystem_delegate_t;

/// Create a new build system instance.
///
/// The C-string members of the invocation structure are immediately copied upon
/// construction of the build system, and can safely be released upon return.
LLBUILD_EXPORT llb_buildsystem_t*
llb_buildsystem_create(llb_buildsystem_delegate_t delegate,
                       llb_buildsystem_invocation_t invocation);

/// Destroy a build system invocation.
LLBUILD_EXPORT void
llb_buildsystem_destroy(llb_buildsystem_t* system);

/// Initialize the build system.
///
/// This will load the build manifest and apply any other options (for example,
/// attaching the database).
///
/// Clients may use a single build system for many separate build
/// invocations. When used this way, the underlying system will transparently
/// cache the contents of the manifest as well as database results which can
/// result in a significant performance improvement for builds in little
/// substantive work is performed.
///
/// \returns True on success, or false if the system could not be
/// initialized. It is a programmatic error to attempt to use the system after
/// initialization has failed.
LLBUILD_EXPORT bool
llb_buildsystem_initialize(llb_buildsystem_t* system);

/// Build the named target.
///
/// It is an unchecked error for the client to request multiple builds
/// concurrently.
///
/// This will automatically initialize the build system if it has not already
/// been initialized.
///
/// \param key Name of the target to build.
/// \returns True on success, or false if the build was aborted (for example, if
/// a cycle was discovered).
LLBUILD_EXPORT bool
llb_buildsystem_build(llb_buildsystem_t* system, const llb_data_t* key);

/// Build a single node.
///
/// It is an unchecked error for the client to request multiple builds
/// concurrently.
///
/// This will automatically initialize the build system if it has not already
/// been initialized.
///
/// \param key Path to the node to build.
/// \returns True on success, or false if the build was aborted (for example, if
/// a cycle was discovered).
LLBUILD_EXPORT bool
llb_buildsystem_build_node(llb_buildsystem_t* system, const llb_data_t* key);

/// Cancel any ongoing build.
///
/// This method may be called from any thread.
LLBUILD_EXPORT void
llb_buildsystem_cancel(llb_buildsystem_t* system);

/// @}

/// @name Tool APIs
/// @{

/// Delegate structure for callbacks required by a build tool.
typedef struct llb_buildsystem_tool_delegate_t_ {
  /// User context pointer.
  void* context;

  // FIXME: Need to support configuration behaviors.
  
  /// Called by the build system to create an individual command with this tool
  /// type.
  ///
  /// Xparam The name of the command to create.
  /// Xreturns If the tool is recognized, the client should return an instance
  /// of the tool created with \see llb_buildsystem_create_tool(). If the tool
  /// is not recognized, the client should return null.
  llb_buildsystem_command_t* (*create_command)(void* context,
                                               const llb_data_t* name);

  // FIXME: Support dynamic tool commands.
} llb_buildsystem_tool_delegate_t;

/// Create a new tool instance.
///
/// A tool is the top-level entity responsible for defining custom build system
/// behaviors. Once defined, tools may be used to define new types of commands.
LLBUILD_EXPORT llb_buildsystem_tool_t*
llb_buildsystem_tool_create(const llb_data_t* name,
                            llb_buildsystem_tool_delegate_t delegate);

/// @}

/// @name Command APIs
/// @{

/// Opaque handle to a queue job context.
typedef struct llb_buildsystem_queue_job_context_t_
  llb_buildsystem_queue_job_context_t;

/// Opaque handle to a build key.
typedef struct llb_buildsystem_key_t_
  llb_buildsystem_key_t;

/// Opaque handle to a build value.
typedef struct llb_buildsystem_value_t_
  llb_buildsystem_value_t;

/// Opaque handle to the interfaces for a running command.
//
// FIXME: Find a better name for this, it is too long.
typedef struct llb_buildsystem_command_interface_t_
  llb_buildsystem_command_interface_t;
  
/// Delegate structure for callbacks required by an external build command.
typedef struct llb_buildsystem_external_command_delegate_t_ {
  /// User context pointer.
  void* context;

  /// Called to get a signature which represents the internal state of the
  /// command which is not tracked by any other attribute visible to the build
  /// system (for example, a declared input or output). This signature is
  /// compared with previous executions of the command when determining whether
  /// or not it needs to rerun.
  ///
  /// The contents *MUST* be returned in a new buffer allocated with \see
  /// malloc().
  void (*get_signature)(void* context, llb_buildsystem_command_t* command,
                        llb_data_t* data_out);

  /// Called by the build system's execution queue after the command's inputs
  /// are available and the execution queue is ready to schedule the command.
  ///
  /// This function is responsible for completing the work and notifying the
  /// system using the command interface. It can add additional work onto the
  /// execution queue, so long as it arranges to only notify the system of
  /// completion once all that work is complete.
  bool (*execute_command)(void* context, llb_buildsystem_command_t* command,
                          llb_buildsystem_command_interface_t* bsci,
                          llb_task_t* task,
                          llb_buildsystem_queue_job_context_t* job_context);
} llb_buildsystem_external_command_delegate_t;

/// Create a new external command instance.
///
/// An external command is one which is run externally to the build system (not
/// necessarily out of process) and which interacts using the standard notions
/// of files and virtual nodes.
///
/// It defines common base behaviors which make sense for all such tools (for
/// example, capturing the signatures of output nodes and a standard behavior
/// for determining when the command needs to be recomputed).
LLBUILD_EXPORT llb_buildsystem_command_t*
llb_buildsystem_external_command_create(
    const llb_data_t* name,
    llb_buildsystem_external_command_delegate_t delegate);

/// Get the name of the given command.
///
/// \param key_out On return, contains a pointer to the name of the command.
LLBUILD_EXPORT void
llb_buildsystem_command_get_name(llb_buildsystem_command_t* command,
                                 llb_data_t* key_out);

/// Get the description for the given command.
///
/// \returns The command description, as a new C string. The client is
/// responsible for calling \see free() on the result.
//
// FIXME: This API most likely doesn't belong.
LLBUILD_EXPORT char*
llb_buildsystem_command_get_description(llb_buildsystem_command_t* command);

/// Get the verbose description for the given command.
///
/// \returns The verbose command description, as a new C string. The client is
/// responsible for calling \see free() on the result.
//
// FIXME: This API most likely doesn't belong.
LLBUILD_EXPORT char*
llb_buildsystem_command_get_verbose_description(
    llb_buildsystem_command_t* command);

/// @}

#endif
