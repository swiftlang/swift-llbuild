//
//  BuildDB-C-API.c
//  libllbuild
//
//  Copyright © 2019 Apple Inc. All rights reserved.
//

#include <llbuild/llbuild.h>

#include "llbuild/Core/BuildDB.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <mutex>

using namespace llbuild;
using namespace llbuild::core;

namespace {

class CAPIBuildDBKeysResult {
private:
  std::vector<KeyType> keys;
  
public:
  CAPIBuildDBKeysResult(std::vector<KeyType> keys): keys(keys) { }
  
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
  /// The key table, used when there is no database.
  llvm::StringMap<KeyID> keyTable;
  
  /// The mutex that protects the key table.
  std::mutex keyTableMutex;
  
  std::unique_ptr<BuildDB> _db;
  
public:
  CAPIBuildDB(StringRef path, uint32_t clientSchemaVersion, std::string *error_out) {
    _db = createSQLiteBuildDB(path, clientSchemaVersion, error_out);
    _db.get()->attachDelegate(this);
  }
  
  virtual KeyID getKeyID(const KeyType& key) override {
    std::lock_guard<std::mutex> guard(keyTableMutex);
    
    // The RHS of the mapping is actually ignored, we use the StringMap's ptr
    // identity because it allows us to efficiently map back to the key string
    // in `getRuleInfoForKey`.
    auto it = keyTable.insert(std::make_pair(key, 0)).first;
    return (KeyID)(uintptr_t)it->getKey().data();
  }
  
  virtual KeyType getKeyForID(KeyID key) override {
    // Note that we don't need to lock `keyTable` here because the key entries
    // themselves don't change once created.
    return llvm::StringMapEntry<KeyID>::GetStringMapEntryFromKeyData(
                                                                     (const char*)(uintptr_t)key).getKey();
  }
  
  const bool lookupRuleResult(KeyID keyID, Result *result_out, std::string *error_out) {
    auto ruleKey = this->getKeyForID(keyID);
    return _db.get()->lookupRuleResult(keyID, ruleKey, result_out, error_out);
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

const llb_database_result_t mapResult(Result result) {
  
  auto count = result.dependencies.size();
  auto data = result.dependencies.data();
  auto size = sizeof(data) * count;
  KeyID *deps = (KeyID*)malloc(size);
  memcpy(deps, data, size);
  
  return llb_database_result_t {
    mapData(result.value),
    result.signature.value,
    result.computedAt,
    result.builtAt,
    deps,
    static_cast<uint32_t>(count)
  };
}

void llb_database_destroy_result(llb_database_result_t *result) {
  delete result->dependencies;
}

const llb_database_t* llb_database_create(
                                    char *path,
                                    uint32_t clientSchemaVersion,
                                    llb_data_t *error_out) {
  std::string error;
  
  auto database = new CAPIBuildDB(StringRef(path), clientSchemaVersion, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
  
  return (llb_database_t *)database;
}

void llb_database_destroy(llb_database_t *database) {
  delete (CAPIBuildDB *)database;
}

const bool llb_database_lookup_rule_result(llb_database_t *database, llb_database_key_id keyID, llb_database_result_t *result_out, llb_data_t *error_out) {
  
  auto db = (CAPIBuildDB *)database;
  
  std::string error;
  Result result;
  
  auto stored = db->lookupRuleResult(keyID, &result, &error);
  
  if (result_out) {
    *result_out = mapResult(result);
  }
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
  
  return stored;
}

const bool llb_database_build_started(llb_database_t *database, llb_data_t *error_out) {
  auto db = (CAPIBuildDB *)database;
  
  std::string error;
  auto success = db->buildStarted(&error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
  
  return success;
}

void llb_database_build_complete(llb_database_t *database) {
  auto db = (CAPIBuildDB *)database;
  db->buildComplete();
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
