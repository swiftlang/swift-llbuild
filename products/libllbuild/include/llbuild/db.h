//
//  db.h
//  llbuild
//
//  Copyright © 2019 Apple Inc. All rights reserved.
//

#ifndef db_h
#define db_h

/// Defines a key identifier _(should match \see KeyID in BuildEngine.h)_
typedef uint64_t llb_database_key_id;
/// Defines a key _(should match \see KeyType in BuildEngine.h)_
typedef const char* llb_database_key_type;

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
  
  /// A list of the dependencies of the computed task (\see dependencies_count for getting the count).
  /// When the result is not needed anymore, call \see llb_database_destroy_result!
  llb_database_key_id *dependencies;
  
  /// The number of dependencies for iterating over \see dependencies
  uint32_t dependencies_count;
} llb_database_result_t;

/// Destroys a result object by freeing its memory
LLBUILD_EXPORT void
llb_database_destroy_result(llb_database_result_t *result);

/// Opaque handler to a database
typedef struct llb_database_t_ llb_database_t;

/// Create a new build database instance
LLBUILD_EXPORT const llb_database_t* llb_database_create(char *path, uint32_t clientSchemaVersion, llb_data_t *error_out);

/// Destroy a build system instance
LLBUILD_EXPORT void
llb_database_destroy(llb_database_t *database);

/// Get the current build iteration from the database
LLBUILD_EXPORT const uint64_t
llb_database_get_current_iteration(llb_database_t *database, bool *success_out, llb_data_t *error_out);

/// Set the current build iteration to the database
LLBUILD_EXPORT void
llb_database_set_current_iteration(llb_database_t *database, uint64_t value, llb_data_t *error_out);

/// Lookup the result of a rule in the database. result_out needs to be destroyed by calling llb_database_destroy_result.
LLBUILD_EXPORT const bool
llb_database_lookup_rule_result(llb_database_t *database, llb_database_key_id keyID, llb_database_result_t *result_out, llb_data_t *error_out);

// TODO: Rule is currently not supported
//LLBUILD_EXPORT bool
//llb_database_set_rule_result(llb_database_t *database, llb_database_key_id keyID, llb_rule_t rule, llb_database_result_t result, char **error_out);

/// Start an exclusive session in the database
LLBUILD_EXPORT const bool
llb_database_build_started(llb_database_t *database, llb_data_t *error_out);

/// End the previously started exclusive session in the database
LLBUILD_EXPORT void
llb_database_build_complete(llb_database_t *database);

/// Opaque pointer to a fetch result for getting all keys from the database
typedef struct llb_database_result_keys_t_ llb_database_result_keys_t;

/// Method for getting the number of keys from a result keys object
LLBUILD_EXPORT const llb_database_key_id
llb_database_result_keys_get_count(llb_database_result_keys_t *result);

/// Method for getting the key for a given id from a result keys object
LLBUILD_EXPORT void
llb_database_result_keys_get_key_at_index(llb_database_result_keys_t *result, llb_database_key_id keyID, llb_data_t *key_out);

/// Destroys the given result keys object, call this when the object is not used anymore
LLBUILD_EXPORT void
llb_database_destroy_result_keys(llb_database_result_keys_t *result);

/// Fetch all keys from the database. The keysResult_out object needs to be destroyed when not used anymore via \see llb_database_destroy_result_keys
LLBUILD_EXPORT const bool
llb_database_get_keys(llb_database_t *database, llb_database_result_keys_t **keysResult_out, llb_data_t *error_out);

/// Dumps an overview of the database's content to stdout
LLBUILD_EXPORT void
llb_database_dump(llb_database_t *database);

#endif /* db_h */
