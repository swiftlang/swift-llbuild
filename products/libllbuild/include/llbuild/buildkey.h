//===- buildkey.h -------------------------------------------------*- C -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
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

#ifndef LLBUILD_PUBLIC_BUILDKEY_H
#define LLBUILD_PUBLIC_BUILDKEY_H

#ifndef LLBUILD_PUBLIC_LLBUILD_H
#error Clients must include the "llbuild.h" umbrella header.
#endif

LLBUILD_ASSUME_NONNULL_BEGIN

typedef struct llb_build_key_ llb_build_key;

LLBUILD_EXPORT llb_build_key *llb_build_key_make(llb_build_key_t key);
LLBUILD_EXPORT void llb_build_key_get_key_data(llb_build_key *key, void *_Nullable context, void (*iteration)(void *_Nullable context, uint8_t data));
LLBUILD_EXPORT llb_build_key_kind_t llb_build_key_get_kind(llb_build_key *key);

LLBUILD_EXPORT void llb_build_key_destroy(llb_build_key *key);

LLBUILD_EXPORT char llb_build_key_identifier_for_kind(llb_build_key_kind_t kind);
LLBUILD_EXPORT llb_build_key_kind_t llb_build_key_kind_for_identifier(char identifier);

// Command
LLBUILD_EXPORT llb_build_key *llb_build_key_make_command(const char *name);
LLBUILD_EXPORT void llb_build_key_get_command_name(llb_build_key *key, llb_data_t *out_name);

// Custom Task
LLBUILD_EXPORT llb_build_key *llb_build_key_make_custom_task(const char *name, const char *taskData);
LLBUILD_EXPORT void llb_build_key_get_custom_task_name(llb_build_key *key, llb_data_t *out_name);
LLBUILD_EXPORT void llb_build_key_get_custom_task_data(llb_build_key *key, llb_data_t *out_task_data);

// Directory Contents
LLBUILD_EXPORT llb_build_key *llb_build_key_make_directory_contents(const char *path);
LLBUILD_EXPORT void llb_build_key_get_directory_path(llb_build_key *key, llb_data_t *out_path);

typedef void (*IteratorFunction)(void *_Nullable context, llb_data_t data);

// Filtered Directory Contents
LLBUILD_EXPORT llb_build_key *llb_build_key_make_filtered_directory_contents(const char *path, const char *_Nonnull const *_Nonnull filters, size_t count_filters);
LLBUILD_EXPORT void llb_build_key_get_filtered_directory_path(llb_build_key *key, llb_data_t *out_path);
LLBUILD_EXPORT void llb_build_key_get_filtered_directory_filters(llb_build_key *key, void *_Nullable context, IteratorFunction  iterator);

// Directory Tree Signature
LLBUILD_EXPORT llb_build_key *llb_build_key_make_directory_tree_signature(const char *path, const char *_Nonnull const *_Nonnull filters, size_t count_filters);
LLBUILD_EXPORT void llb_build_key_get_directory_tree_signature_path(llb_build_key *key, llb_data_t *out_path);
LLBUILD_EXPORT void llb_build_key_get_directory_tree_signature_filters(llb_build_key *key, void *_Nullable context, IteratorFunction  iterator);

// Directory Tree Structure Signature
LLBUILD_EXPORT llb_build_key *llb_build_key_make_directory_tree_structure_signature(const char *path);
LLBUILD_EXPORT void llb_build_key_get_directory_tree_structure_signature_path(llb_build_key *key, llb_data_t *out_path);

// Node
LLBUILD_EXPORT llb_build_key *llb_build_key_make_node(const char *path);
LLBUILD_EXPORT void llb_build_key_get_node_path(llb_build_key *key, llb_data_t *out_path);

// Stat
LLBUILD_EXPORT llb_build_key *llb_build_key_make_stat(const char *path);
LLBUILD_EXPORT void llb_build_key_get_stat_path(llb_build_key *key, llb_data_t *out_path);

// Target
LLBUILD_EXPORT llb_build_key *llb_build_key_make_target(const char *name);
LLBUILD_EXPORT void llb_build_key_get_target_name(llb_build_key *key, llb_data_t *out_name);

LLBUILD_ASSUME_NONNULL_END

#endif
