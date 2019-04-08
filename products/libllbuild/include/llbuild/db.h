//
//  db.h
//  llbuild
//
//  Created by Benjamin Herzog on 4/4/19.
//  Copyright © 2019 Apple Inc. All rights reserved.
//

#ifndef db_h
#define db_h

typedef uint64_t llb_database_key_id;
typedef const char* llb_database_key_type;

typedef struct llb_database_result_t_ {
  
  llb_data_t value;
  
  uint64_t signature;
  
  uint64_t computed_at;
  
  uint64_t built_at;
  
  llb_database_key_id *dependencies;
  
  uint32_t dependencies_count;
} llb_database_result_t;

typedef struct llb_database_key_t_ {
  llb_database_key_type value;
  uint32_t size;
} llb_database_key_t;

typedef struct llb_database_delegate_t_ {
  /// User context pointer.
  void* context;
  
  llb_database_key_id (*get_key_id)(void *context, const llb_database_key_type key);
  
  llb_database_key_type (*get_key_for_id)(void *context, llb_database_key_id key);
  
} llb_database_delegate_t;

/// Opaque handler to a database
typedef struct llb_database_t_ llb_database_t;

/// Create a new build database instance
LLBUILD_EXPORT llb_database_t* llb_database_create(char *path, uint32_t clientSchemaVersion, llb_database_delegate_t delegate, char **error_out);

/// Destroy a build system instance
LLBUILD_EXPORT void
llb_database_destroy(llb_database_t *database);

LLBUILD_EXPORT uint64_t
llb_database_get_current_iteration(llb_database_t *database, bool *success_out, char **error_out);

LLBUILD_EXPORT void
llb_database_set_current_iteration(llb_database_t *database, uint64_t value, char **error_out);

LLBUILD_EXPORT bool
llb_database_lookup_rule_result(llb_database_t *database, llb_database_key_id keyID, llb_database_key_type ruleKey, llb_database_result_t *result_out, char **error_out);

// TODO: Rule is currently not supported
//LLBUILD_EXPORT bool
//llb_database_set_rule_result(llb_database_t *database, llb_database_key_id keyID, llb_rule_t rule, llb_database_result_t result, char **error_out);

LLBUILD_EXPORT bool
llb_database_build_started(llb_database_t *database, char **error_out);

LLBUILD_EXPORT void
llb_database_build_complete(llb_database_t *database);

typedef struct llb_database_result_keys_t_ llb_database_result_keys_t;

LLBUILD_EXPORT uint32_t
llb_database_result_keys_get_count(llb_database_result_keys_t *result);

LLBUILD_EXPORT llb_database_key_type
llb_database_result_keys_get_key_for_id(llb_database_result_keys_t *result, llb_database_key_id key_id);

LLBUILD_EXPORT llb_database_key_id
llb_database_result_keys_get_id_for_key(llb_database_result_keys_t *result, llb_database_key_type key);

LLBUILD_EXPORT void
llb_database_destroy_result_keys(llb_database_result_keys_t *result);

LLBUILD_EXPORT bool
llb_database_get_keys(llb_database_t *database, llb_database_result_keys_t **keysResult_out, char **error_out);

LLBUILD_EXPORT void
llb_database_dump(llb_database_t *database);

#endif /* db_h */
