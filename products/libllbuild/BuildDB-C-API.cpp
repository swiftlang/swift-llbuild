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

#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/Core/BuildDB.h"

#include "BuildKey-C-API-Private.h"

#include <mutex>

using namespace llbuild;
using namespace llbuild::core;
using namespace llbuild::buildsystem;

namespace {

  class CAPIBuildDBKeysResult {
  private:
    const std::vector<KeyType> keys;
    
  public:
    CAPIBuildDBKeysResult(const std::vector<KeyType>& keys): keys(keys) { }
    
    CAPIBuildDBKeysResult(const CAPIBuildDBKeysResult&) LLBUILD_DELETED_FUNCTION;
    CAPIBuildDBKeysResult& operator=(const CAPIBuildDBKeysResult&) LLBUILD_DELETED_FUNCTION;
    
    const size_t size() {
      return keys.size();
    }
    
    llb_build_key_t *keyAtIndex(int32_t index) {
      auto buildKey = BuildKey::fromData(keys[index]);
      return (llb_build_key_t *)new CAPIBuildKey(buildKey);
    }
  };

  class CAPIBuildDB: public BuildDBDelegate {
    /// The key table, for memory caching of key to key id mapping.
    llvm::StringMap<KeyID> keyTable;
    
    /// The mutex that protects the key table.
    std::mutex keyTableMutex;
    
    std::unique_ptr<BuildDB> _db;
    
    CAPIBuildDB(StringRef path, uint32_t clientSchemaVersion, std::string *error_out) {
      _db = createSQLiteBuildDB(path, clientSchemaVersion, /* recreateUnmatchedVersion = */ false, error_out);
    }
    
  public:
    static CAPIBuildDB *create(StringRef path, uint32_t clientSchemaVersion, std::string *error_out) {
      auto databaseObject = new CAPIBuildDB(path, clientSchemaVersion, error_out);
      if (databaseObject->_db == nullptr || !error_out->empty() || !databaseObject->buildStarted(error_out)) {
        delete databaseObject;
        return nullptr;
      }
      
      // The delegate  caches keys for key ids
      databaseObject->_db.get()->attachDelegate(databaseObject);
      
      return databaseObject;
    }
    
    bool isValid(std::string *error_out) {
      if (_db == nullptr) {
        return false;
      }
      
      return buildStarted(error_out);
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
    
    const bool lookupRuleResult(const KeyType &key, Result *result_out, std::string *error_out) {
      return _db.get()->lookupRuleResult(this->getKeyID(key), key, result_out, error_out);
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

const llb_data_t mapData(std::vector<uint8_t> input) {
  const auto data = input.data();
  const auto size = sizeof(data) * input.size();
  const auto newData = (uint8_t *)malloc(size);
  memcpy(newData, data, size);
  
  return llb_data_t {
    input.size(),
    newData
  };
}

const llb_database_result_t mapResult(CAPIBuildDB &db, Result result) {
  auto count = result.dependencies.size();
  auto size = sizeof(llb_build_key_t*) * count;
  llb_build_key_t **deps = (llb_build_key_t **)malloc(size);
  int index = 0;
  for (auto keyID: result.dependencies) {
    auto buildKey = db.getKeyForID(keyID);
    auto internalBuildKey = BuildKey::fromData(buildKey);
    deps[index] = (llb_build_key_t *)new CAPIBuildKey(internalBuildKey);
    index++;
  }
  
  return llb_database_result_t {
    mapData(result.value),
    result.signature.value,
    result.computedAt,
    result.builtAt,
    result.start,
    result.end,
    deps,
    static_cast<uint32_t>(count)
  };
}

void llb_database_destroy_result(const llb_database_result_t *result) {
  delete result->value.data;
  delete result->dependencies;
}

const llb_database_t* llb_database_open(
                                        char *path,
                                        uint32_t clientSchemaVersion,
                                        llb_data_t *error_out) {
  std::string error;
  
  auto database = CAPIBuildDB::create(StringRef(path), clientSchemaVersion, &error);
  
  if (!error.empty()) {
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

const bool llb_database_lookup_rule_result(llb_database_t *database, llb_build_key_t *key, llb_database_result_t *result_out, llb_data_t *error_out) {
  
  auto db = (CAPIBuildDB *)database;
  
  std::string error;
  Result result;
  
  auto stored = db->lookupRuleResult(((CAPIBuildKey *)key)->getInternalBuildKey().toData(), &result, &error);
  
  if (result_out) {
    *result_out = mapResult(*db, result);
  }
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
  
  return stored;
}

const llb_database_key_id llb_database_result_keys_get_count(llb_database_result_keys_t *result) {
  auto resultKeys = (CAPIBuildDBKeysResult *)result;
  return resultKeys->size();
}

llb_build_key_t * llb_database_result_keys_get_key_at_index(llb_database_result_keys_t *result, int32_t index) {
  auto resultKeys = (CAPIBuildDBKeysResult *)result;
  return resultKeys->keyAtIndex(index);
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

