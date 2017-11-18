//===- core.h -----------------------------------------------------*- C -*-===//
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
// These are the C API interfaces to the llbuild Core component.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_PUBLIC_CORE_H
#define LLBUILD_PUBLIC_CORE_H

#ifndef LLBUILD_PUBLIC_LLBUILD_H
#error Clients must include the "llbuild.h" umbrella header.
#endif

#include <stdbool.h>
#include <stdint.h>

/// @name Build Engine
/// @{

/// Opaque handle to a build engine.
typedef struct llb_buildengine_t_ llb_buildengine_t;

/// Opaque handle to an executing task.
typedef struct llb_task_t_ llb_task_t;

/// Representation for a blob of bytes.
///
/// NOTE: There is no defined memory management model for the provided data
/// pointer. Instead, all uses of the llb_data_t when provided as input
/// arguments to llbuild are required to ensure the data pointer is live for the
/// life of the call. When used as an output parameter, the build system will
/// ensure that the data pointer is live at least until the next llbuild API
/// call is made.
typedef struct llb_data_t_ {
    uint64_t length;
    const uint8_t* data;
} llb_data_t;

/// Status kind indications for Rules.
typedef enum {
    /// Indicates the rule is being scanned.
    llb_rule_is_scanning = 0,

    /// Indicates the rule is up-to-date, and doesn't need to run.
    llb_rule_is_up_to_date = 1,

    /// Indicates the rule was run, and is now complete.
    llb_rule_is_complete = 2
} llb_rule_status_kind_t;

/// Rule representation.
typedef struct llb_rule_t_ llb_rule_t;
struct llb_rule_t_ {
    /// User context pointer.
    void* context;

    /// The key this rule computes.
    llb_data_t key;

    /// The callback to create a task for computing this rule.
    ///
    /// Xparam context The task context pointer.
    /// Xparam engine_context The context pointer for the engine delegate.
    llb_task_t* (*create_task)(void* context, void* engine_context);

    /// The callback to check if a previously computed result is still valid.
    ///
    /// Xparam context The task context pointer.
    /// Xparam engine_context The context pointer for the engine delegate.
    /// Xparam rule The rule under consideration.
    /// Xparam result The previously computed result for the rule.
    bool (*is_result_valid)(void* context, void* engine_context,
                            const llb_rule_t* rule, const llb_data_t* result);

    /// Called to indicate a change in the rule status.
    void (*update_status)(void* context, void* engine_context,
                          llb_rule_status_kind_t kind);
};

/// Delegate structure for callbacks required by the build engine.
typedef struct llb_buildengine_delegate_t_ {
    /// User context pointer.
    void* context;

    /// Callback for releasing the user context, called on engine destruction.
    void (*destroy_context)(void* context);

    /// Callback for resolving keys to the rule that should be used to compute
    /// them.
    ///
    /// Xparam context The user context pointer.
    /// Xparam key The key being looked up.
    /// Xparam rule_out [out] On return, the rule to use to build the given key.
    void (*lookup_rule)(void* context,
                        const llb_data_t* key,
                        llb_rule_t* rule_out);

    /// Callback for fatal errors the build engine encounters.
    ///
    /// Xparam context The user context pointer.
    /// Xparam message Error message.
    void (*error)(void* context, const char* message);
} llb_buildengine_delegate_t;

/// Create a new build engine object.
///
/// \param delegate The delegate to use for build engine operations.
LLBUILD_EXPORT llb_buildengine_t*
llb_buildengine_create(llb_buildengine_delegate_t delegate);

/// Destroy a build engine.
LLBUILD_EXPORT void
llb_buildengine_destroy(llb_buildengine_t* engine);

/// Create a new build engine object.
///
/// \param path The path to create or load the database from.
///
/// \param schema_version The schema version used by the client for this
/// database. Any existing database will be checked against this schema version
/// to determine if existing results can be used.
///
/// \param error_out On failure, a pointer to a C-string describing the
/// error. The string must be released using \see free().
///
/// \returns True on success.
LLBUILD_EXPORT bool
llb_buildengine_attach_db(llb_buildengine_t* engine,
                          const llb_data_t* path,
                          uint32_t schema_version,
                          char **error_out);

/// Build the result for a particular key.
///
/// \param engine The engine to operate on.
/// \param key The key to build.
/// \param result_out [out] On return, the result of computing the given key.
LLBUILD_EXPORT void
llb_buildengine_build(llb_buildengine_t* engine, const llb_data_t* key,
                      llb_data_t* result_out);

/// Register the given task, in response to a Rule evaluation.
///
/// The engine tasks ownership of the \arg task, and it is expected to
/// subsequently be returned as the task to execute for a rule evaluation.
///
/// \returns The provided task, for the convenience of the client.
LLBUILD_EXPORT llb_task_t*
llb_buildengine_register_task(llb_buildengine_t* engine, llb_task_t* task);

/// Specify the given \arg Task depends upon the result of computing \arg Key.
///
/// The result, when available, will be provided to the task via \see
/// Task::provideValue(), supplying the provided \arg InputID to allow the
/// task to identify the particular input.
///
/// NOTE: It is an unchecked error for a task to request the same input value
/// multiple times.
///
/// \param input_id An arbitrary value that may be provided by the client to use
/// in efficiently associating this input. The range of this parameter is
/// intentionally chosen to allow a pointer to be provided, but note that all
/// input IDs greater than \see kMaximumInputID are reserved for internal use by
/// the engine.
LLBUILD_EXPORT void
llb_buildengine_task_needs_input(llb_buildengine_t* engine, llb_task_t* task,
                                 const llb_data_t* key, uintptr_t input_id);

/// Specify that the given \arg task must be built subsequent to the
/// computation of \arg key.
///
/// The value of the computation of \arg key is not available to the task, and
/// the only guarantee the engine provides is that if \arg key is computed
/// during a build, then \arg task will not be computed until after it.
LLBUILD_EXPORT void
llb_buildengine_task_must_follow(llb_buildengine_t* engine, llb_task_t* task,
                                 const llb_data_t* key);

/// Inform the engine of an input dependency that was discovered by the task
/// during its execution, a la compiler generated dependency files.
///
/// This call may only be made after a task has received all of its inputs;
/// inputs discovered prior to that point should simply be requested as normal
/// input dependencies.
///
/// Such a dependency is not used to provide additional input to the task,
/// rather it is a way for the task to report an additional input which should
/// be considered the next time the rule is evaluated. The expected use case
/// for a discovered dependency is is when a processing task cannot predict
/// all of its inputs prior to being run, but can presume that any unknown
/// inputs already exist. In such cases, the task can go ahead and run and can
/// report the all of the discovered inputs as it executes. Once the task is
/// complete, these inputs will be recorded as being dependencies of the task
/// so that it will be recomputed when any of the inputs change.
///
/// It is legal to call this method from any thread, but the caller is
/// responsible for ensuring that it is never called concurrently for the same
/// task.
LLBUILD_EXPORT void
llb_buildengine_task_discovered_dependency(llb_buildengine_t* engine,
                                           llb_task_t* task,
                                           const llb_data_t* key);

/// Called by a task to indicate it has completed and to provide its value.
///
/// It is legal to call this method from any thread.
///
/// \param value The new value for the task's rule.
///
/// \param force_change If true, treat the value as changed and trigger
/// dependents to rebuild, even if the value itself is not different from the
/// prior result.
LLBUILD_EXPORT void
llb_buildengine_task_is_complete(llb_buildengine_t* engine, llb_task_t* task,
                                 const llb_data_t* value, bool force_change);

/// @}

/// @name Build Engine Task
/// @{

/// Delegate structure for callbacks required by a task.
typedef struct llb_task_delegate_t_ {
    /// User context pointer.
    void* context;

    /// The callback indicating the task has been started.
    ///
    /// Xparam context The task context pointer.
    /// Xparam engine_context The context pointer for the engine delegate.
    /// Xparam task The task which is being started.
    void (*start)(void* context, void* engine_context, llb_task_t* task);

    /// The callback to provide a requested input value to the task.
    ///
    /// Xparam context The task context pointer.
    /// Xparam engine_context The context pointer for the engine delegate.
    /// Xparam task The task which is being started.
    void (*provide_value)(void* context, void* engine_context, 
                          llb_task_t* task,
                          uintptr_t input_id, const llb_data_t* value);

    /// The callback indicating that all requested inputs have been provided.
    ///
    /// The task is expected to call \see llb_buildengine_task_is_complete() at
    /// some point in the future for the task, to provide the result of
    /// executing the task.
    ///
    /// Xparam context The task context pointer.
    /// Xparam engine_context The context pointer for the engine delegate.
    /// Xparam task The task which is being started.
    void (*inputs_available)(void* context, void* engine_context,
                             llb_task_t* task);
} llb_task_delegate_t;

/// Create a task object.
LLBUILD_EXPORT llb_task_t*
llb_task_create(llb_task_delegate_t delegate);

/// @}

#endif
