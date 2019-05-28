//===-- BuildDB-C-API.cpp ---------------------------------------------===//
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

#include <llbuild/llbuild.h>

#include "llbuild/Core/BuildDB.h"

#include <mutex>

using namespace llbuild;
using namespace llbuild::core;

namespace {
  
  class CAPIBuildDBKeysResult {
  private:
    const std::vector<KeyType>& keys;
    
  public:
    CAPIBuildDBKeysResult(const std::vector<KeyType>& keys): keys(keys) { }
    
    CAPIBuildDBKeysResult(const CAPIBuildDBKeysResult&) LLBUILD_DELETED_FUNCTION;
    CAPIBuildDBKeysResult& operator=(const CAPIBuildDBKeysResult&) LLBUILD_DELETED_FUNCTION;
    
    const uint32_t size() {
      return keys.size();
    }
    
    const KeyType keyAtIndex(KeyID index) {
      return keys[index];
    }
  };
  
  class CAPIBuildDB: public BuildDBDelegate {
    /// The key table, for memory caching of key to key id mapping.
    llvm::StringMap<KeyID> keyTable;
    
    /// The mutex that protects the key table.
    std::mutex keyTableMutex;
    
    std::unique_ptr<BuildDB> _db;
    
  public:
    CAPIBuildDB(StringRef path, uint32_t clientSchemaVersion, std::string *error_out) {
      _db = createSQLiteBuildDB(path, clientSchemaVersion, /* recreateUnmatchedVersion = */ false, error_out);
      if (error_out != NULL) {
        return;
      }
      // The delegate  caches keys for key ids
      _db.get()->attachDelegate(this);
      
      if (!buildStarted(error_out)) {
        buildComplete();
        return;
      }
    }
    
    virtual const KeyID getKeyID(const KeyType& key) override {
      std::lock_guard<std::mutex> guard(keyTableMutex);
      
      // The RHS of the mapping is actually ignored, we use the StringMap's ptr
      // identity because it allows us to efficiently map back to the key string
      // in `getRuleInfoForKey`.
      auto it = keyTable.insert(std::make_pair(key, 0)).first;
      return (KeyID)(uintptr_t)it->getKey().data();
    }
    
    virtual KeyType getKeyForID(const KeyID key) override {
      // Note that we don't need to lock `keyTable` here because the key entries
      // themselves don't change once created.
      return llvm::StringMapEntry<KeyID>::GetStringMapEntryFromKeyData(
                                                                       (const char*)(uintptr_t)key).getKey();
    }
    
    const bool buildStarted(std::string *error_out) {
      return _db.get()->buildStarted(error_out);
    }
    
    void buildComplete() {
      _db.get()->buildComplete();
    }
    
    const bool getKeys(std::vector<KeyType>& keys_out, std::string *error_out) {
      return _db.get()->getKeys(keys_out, error_out);
    }
  };
  
}

const llb_database_t* llb_database_open(
                                          char *path,
                                          uint32_t clientSchemaVersion,
                                          llb_data_t *error_out) {
  std::string error;
  
  auto database = new CAPIBuildDB(StringRef(path), clientSchemaVersion, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
    delete database;
    return nullptr;
  }
  
  return (llb_database_t *)database;
}

void llb_database_destroy(llb_database_t *database) {
  auto db = (CAPIBuildDB *)database;
  db->buildComplete();
  delete db;
}

const llb_database_key_id llb_database_result_keys_get_count(llb_database_result_keys_t *result) {
  auto resultKeys = (CAPIBuildDBKeysResult *)result;
  return resultKeys->size();
}

void llb_database_result_keys_get_key_at_index(llb_database_result_keys_t *result, llb_database_key_id keyID, llb_data_t *key_out) {
  auto resultKeys = (CAPIBuildDBKeysResult *)result;
  auto key = resultKeys->keyAtIndex(keyID);
  key_out->length = key.size();
  key_out->data = (const uint8_t*) strdup(key.data());
}

void llb_database_destroy_result_keys(llb_database_result_keys_t *result) {
  delete (CAPIBuildDBKeysResult *)result;
}

const bool llb_database_get_keys(llb_database_t *database, llb_database_result_keys_t **keysResult_out, llb_data_t *error_out) {
  auto db = (CAPIBuildDB *)database;
  
  std::vector<KeyType> keys;
  std::string error;
  
  auto success = db->getKeys(keys, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
    return success;
  }
  
  auto resultKeys = new CAPIBuildDBKeysResult(keys);
  *keysResult_out = (llb_database_result_keys_t *)resultKeys;
  
  return success;
}

