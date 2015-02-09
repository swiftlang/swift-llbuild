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

#ifndef LLBUILD_PUBLIC_H
#define LLBUILD_PUBLIC_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
#define LLBUILD_EXPORT extern "C"
#elif __GNUC__
#define LLBUILD_EXPORT extern __attribute__((visibility("default")))
#else
#define LLBUILD_EXPORT extern
#endif

/// Get the full version of the llbuild library.
LLBUILD_EXPORT const char* llb_get_full_version_string(void);

/// @name Build Engine
///
/// @{

/// Opaque handle to a build engine.
typedef struct llb_buildengine_t_ llb_buildengine_t;

/// Opaque handle to an executing task.
typedef struct llb_task_t_ llb_task_t;

/// Representation for a blob of bytes.
typedef struct llb_data_t_ {
    uint64_t length;
    const uint8_t* data;
} llb_data_t;

/// Rule representation.
typedef struct llb_rule_t_ llb_rule_t;
struct llb_rule_t_ {
    /// User context pointer.
    void* context;

    /// The key this rule computes.
    llb_data_t key;

    /// The callback to create a task for computing this rule.
    llb_task_t* (*create_task)(void* context,
                               llb_buildengine_t* engine);

    /// The callback to check if a previously computed result is still valid.
    bool (*is_result_valid)(void* context, const llb_rule_t* rule,
                            const llb_data_t* result);
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
} llb_buildengine_delegate_t;

/// Create a new build engine object.
///
/// \param delegate The delegate to use for build engine operations.
LLBUILD_EXPORT llb_buildengine_t*
llb_buildengine_create(llb_buildengine_delegate_t delegate);

/// Destroy a build engine.
LLBUILD_EXPORT void
llb_buildengine_destroy(llb_buildengine_t* engine);

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


/// Called by a task to indicate it has completed and to provide its value.
///
/// It is legal to call this method from any thread.
LLBUILD_EXPORT void
llb_buildengine_task_is_complete(llb_buildengine_t* engine, llb_task_t* task,
                                 const llb_data_t* value);

/// @}

/// @name Build Engine Task
/// @{

/// Delegate structure for callbacks required by a task.
typedef struct llb_task_delegate_t_ {
    /// User context pointer.
    void* context;

    /// The callback indicating the task has been started.
    void (*start)(void* context, llb_task_t* task, llb_buildengine_t* engine);

    /// The callback to provide a requested input value to the task.
    void (*provide_value)(void* context, llb_task_t* task,
                          llb_buildengine_t* engine, uintptr_t input_id,
                          const llb_data_t* value);

    /// The callback indicating that all requested inputs have been provided.
    void (*inputs_available)(void* context, llb_task_t* task,
                             llb_buildengine_t* engine);
} llb_task_delegate_t;

/// Create a task object.
LLBUILD_EXPORT llb_task_t*
llb_task_create(llb_task_delegate_t delegate);

/// @}

#endif
