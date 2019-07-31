//===- db.h --------------------------------------------------*- C -*-===//
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

#ifndef LLBUILD_PUBLIC_LLBUILD_H
#error Clients must include the "llbuild.h" umbrella header.
#endif

#include "buildkey.h"

LLBUILD_ASSUME_NONNULL_BEGIN

/// Defines a key identifier _(should match \see KeyID in BuildEngine.h)_
typedef uint64_t llb_database_key_id LLBUILD_SWIFT_NAME(BuildDBKeyID);
/// Defines a key _(should match \see KeyType in BuildEngine.h)_
typedef const char* llb_database_key_type LLBUILD_SWIFT_NAME(BuildDBKeyType);

/// Defines the result of a task, needs to be
typedef struct llb_database_result_t_ {

   /// The value that resulted from executing the task
  llb_data_t value;

   /// Signature of the node that generated the result
  uint64_t signature;

   /// The build iteration this result was computed at
  uint64_t computed_at;

   /// The build iteration this result was built at
  uint64_t built_at;

  /// The start of the command as a duration since a reference time
  double start;
  
  /// The duration since a reference time of when the command finished computing
  double end;
  
   /// A list of the dependencies of the computed task (\see dependencies_count for getting the count).
  /// When the result is not needed anymore, call \see llb_database_destroy_result!
  llb_build_key_t *_Nullable dependencies;

   /// The number of dependencies for iterating over \see dependencies
  uint32_t dependencies_count;
} llb_database_result_t LLBUILD_SWIFT_NAME(BuildDBResult);

/// Opaque handler to a database
typedef struct llb_database_t_ llb_database_t;

/// Open the database that's saved at the given path by creating a llb_database_t instance. If the creation fails due to an error, nullptr will be returned.
LLBUILD_EXPORT const llb_database_t *_Nullable llb_database_open(char *path, uint32_t clientSchemaVersion, llb_data_t *error_out);

/// Destroy a build system instance
LLBUILD_EXPORT void
llb_database_destroy(llb_database_t *database);

/// Lookup the result of a rule in the database. result_out needs to be destroyed by calling llb_database_destroy_result.
LLBUILD_EXPORT const bool
llb_database_lookup_rule_result(llb_database_t *database, llb_build_key_t key, llb_database_result_t *result_out, llb_data_t *error_out);

/// Destroys a result object by freeing its memory
LLBUILD_EXPORT void
llb_database_destroy_result(const llb_database_result_t *result);

/// Opaque pointer to a fetch result for getting all keys from the database
typedef struct llb_database_result_keys_t_ llb_database_result_keys_t;

/// Method for getting the number of keys from a result keys object
LLBUILD_EXPORT const llb_database_key_id
llb_database_result_keys_get_count(llb_database_result_keys_t *result);

/// Method for getting the key for a given id from a result keys object
LLBUILD_EXPORT llb_build_key_t
llb_database_result_keys_get_key_at_index(llb_database_result_keys_t *result, int32_t index);

/// Destroys the given result keys object, call this when the object is not used anymore
LLBUILD_EXPORT void
llb_database_destroy_result_keys(llb_database_result_keys_t *result);

/// Fetch all keys from the database. The keysResult_out object needs to be destroyed when not used anymore via \see llb_database_destroy_result_keys
LLBUILD_EXPORT const bool
llb_database_get_keys(llb_database_t *database, llb_database_result_keys_t *_Nullable *_Nonnull keysResult_out, llb_data_t *_Nullable error_out);

LLBUILD_ASSUME_NONNULL_END
