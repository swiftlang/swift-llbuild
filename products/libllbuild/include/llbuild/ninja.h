//===- ninja.h ----------------------------------------------------*- C -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2021 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// These are the C API interfaces to the llbuild Ninja component.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_PUBLIC_NINJA_H
#define LLBUILD_PUBLIC_NINJA_H

#if !defined(LLBUILD_PUBLIC_LLBUILD_H) && !defined(__clang_tapi__)
#error Clients must include the "llbuild.h" umbrella header.
#endif

#include "llbuild-defines.h"

LLBUILD_ASSUME_NONNULL_BEGIN

typedef struct llb_ninja_error_t_ *llb_ninja_error_t
LLBUILD_SWIFT_NAME(OpaqueNinjaError);

/// A reference to a string, not necessarily null terminated.
typedef struct llb_string_ref_t_ {
    uint64_t length;
    const char *data;
} llb_string_ref_t LLBUILD_SWIFT_NAME(CStringRef);

/// Key/value pair for a variable in either a rule or statement:
/// ```
/// someVar = value
/// ```
///
/// \see https://ninja-build.org/manual.html#_variables
typedef struct llb_ninja_variable_t_ {
  llb_string_ref_t key;
  llb_string_ref_t value;
} llb_ninja_variable_t LLBUILD_SWIFT_NAME(CNinjaVariable);

/// Representation of a rule and its (possibly special) variables:
/// ```
/// rule cc
///   command = clang -c $in -o $out
/// ```
/// Note that all referenced variables are unevaluated.
///
/// \see https://ninja-build.org/manual.html#_rules
typedef struct llb_ninja_rule_t_ {
  llb_string_ref_t name;
  uint64_t num_variables;
  const llb_ninja_variable_t *variables;
} llb_ninja_rule_t LLBUILD_SWIFT_NAME(CNinjaRule);

/// An evaluated build statement:
/// ```
/// build output.o: cc foo.c
/// ```
///
/// \see https://ninja-build.org/manual.html#_build_statements
// TODO: Add the other special variables, ie. depsFile, rspFile, rspFileContent,
//       and depsStyle
typedef struct llb_ninja_build_statement_t_ {
  const llb_ninja_rule_t *rule;
  llb_string_ref_t command;
  llb_string_ref_t description;
  uint64_t num_explicit_inputs;
  const llb_string_ref_t *explicit_inputs;
  uint64_t num_implicit_inputs;
  const llb_string_ref_t *implicit_inputs;
  uint64_t num_order_only_inputs;
  const llb_string_ref_t *order_only_inputs;
  uint64_t num_outputs;
  const llb_string_ref_t *outputs;
  uint64_t num_variables;
  const llb_ninja_variable_t *variables;
  bool generator;
  bool restat;
} llb_ninja_build_statement_t LLBUILD_SWIFT_NAME(CNinjaBuildStatement);

/// Opaque pointer to the underlying manifest data
typedef struct llb_ninja_raw_manifest_t_ *llb_ninja_raw_manifest_t
LLBUILD_SWIFT_NAME(RawCNinjaManifest);

/// A Ninja manifest file loaded by \see llb_manifest_fs_load. Data referenced
/// by this type is only valid until a corresponding \see llb_manifest_destroy.
/// \c statements and \c targets contain references into the underlying raw
/// manifest and *should not* be used once the manifest is destroyed.
///
/// \c error is non-empty if any errors have occurred.
///
/// See https://ninja-build.org/manual.html#ref_ninja_file
typedef struct llb_ninja_manifest_t_ {
  llb_ninja_raw_manifest_t raw_manifest;
  uint64_t num_statements;
  const llb_ninja_build_statement_t *statements;
  uint64_t num_default_targets;
  const llb_string_ref_t *default_targets;
  llb_string_ref_t error;
} llb_ninja_manifest_t LLBUILD_SWIFT_NAME(CNinjaManifest);

/// Load a Ninja manifest at \p filename, resolving any relative paths using
/// \p working_dir.
LLBUILD_EXPORT llb_ninja_manifest_t llb_manifest_fs_load(
    const char *filename, const char *working_dir);

/// Free the underlying manifest. References in \p manifest are no longer valid
/// after this function has returned.
LLBUILD_EXPORT void llb_manifest_destroy(llb_ninja_manifest_t *manifest);

LLBUILD_ASSUME_NONNULL_END

#endif
