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

class CAPIBuildDB: public BuildDBDelegate {
private:
  /// The key table, for memory caching of key to key id mapping.
  llvm::StringMap<KeyID> keyTable;
  
  /// The mutex that protects the key table.
  std::mutex keyTableMutex;
  
  std::unique_ptr<BuildDB> _db;
  
  /// The mutex that protects the key cache.
  std::mutex keyCacheMutex;
  
  std::unordered_map<KeyID, CAPIBuildKey *> keyCache;
  
  bool keyCacheInitialized = false;
  
  CAPIBuildDB(StringRef path, uint32_t clientSchemaVersion, std::string *error_out) {
    _db = createSQLiteBuildDB(path, clientSchemaVersion, /* recreateUnmatchedVersion = */ false, error_out);
  }
  
  bool fetchKeysIfNecessary(std::string *error) {
    if (keyCacheInitialized) {
      return true;
    }
    
    std::vector<KeyType> keys;
    auto success = getKeys(keys, error);
    
    if (!error->empty()) {
      return success;
    }
    
    return success;
  }
  
  /// This method needs keyCacheMutex to be locked to ensure thread safety.
  void buildUpKeyCache(std::vector<KeyType> &allKeys) {
    if (keyCache.size() == allKeys.size()) {
      // There's not need to build up the cache multiple times
      return;
    }
    
    // Since we're holding the lock already, we don't lock in the destroy method
    destroyKeyCache();
    
    keyCache = std::unordered_map<KeyID, CAPIBuildKey *>();
    keyCache.reserve(allKeys.size());
    for (auto rawKey: allKeys) {
      const KeyID engineID = getKeyID(rawKey);
      keyCache[engineID] = new CAPIBuildKey(BuildKey::fromData(rawKey), engineID);
    }
    keyCacheInitialized = true;
  }
  
  /// This method needs keyCacheMutex to be locked to ensure thread safety.
  void destroyKeyCache() {
    // every key in the keyCache is owned by the database, so we need to destroy them
    for (std::pair<KeyID, CAPIBuildKey *>element: keyCache) {
      llb_build_key_destroy((llb_build_key_t *)element.second);
    }
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
  
  ~CAPIBuildDB() {
    std::lock_guard<std::mutex> guard(keyCacheMutex);
    destroyKeyCache();
  }
  
  CAPIBuildKey *buildKeyForIdentifier(KeyID keyIdentifier, std::string *error) {
    if (!fetchKeysIfNecessary(error)) {
      return nullptr;
    }
    
    std::lock_guard<std::mutex> guard(keyCacheMutex);
    auto buildKey = keyCache[keyIdentifier];
    if (buildKey == nullptr) {
      *error = "Unable to get build key for identifier " + std::to_string(keyIdentifier);
    }
    return buildKey;
  }
  
  virtual const KeyID getKeyID(const KeyType& key) override {
    std::lock_guard<std::mutex> guard(keyTableMutex);
    
    // The RHS of the mapping is actually ignored, we use the StringMap's ptr
    // identity because it allows us to efficiently map back to the key string
    // in `getRuleInfoForKey`.
    auto it = keyTable.insert(std::make_pair(key.str(), KeyID::novalue())).first;
    return KeyID(it->getKey().data());
  }
  
  virtual KeyType getKeyForID(const KeyID key) override {
    // Note that we don't need to lock `keyTable` here because the key entries
    // themselves don't change once created.
    return llvm::StringMapEntry<KeyID>::GetStringMapEntryFromKeyData(
                                                                     (const char*)(uintptr_t)key).getKey();
  }
  
  bool lookupRuleResult(const KeyType &key, Result *result_out, std::string *error_out) {
    return _db.get()->lookupRuleResult(this->getKeyID(key), key, result_out, error_out);
  }
  
  bool buildStarted(std::string *error_out) {
    return _db.get()->buildStarted(error_out);
  }
  
  void buildComplete() {
    _db.get()->buildComplete();
  }
  
  bool getKeys(std::vector<KeyType>& keys_out, std::string *error_out) {
    auto success = _db.get()->getKeys(keys_out, error_out);
    std::lock_guard<std::mutex> guard(keyCacheMutex);
    buildUpKeyCache(keys_out);
    return success;
  }
  
  bool getKeysWithResult(std::vector<KeyType> &keys_out, std::vector<Result> &results_out, std::string *error_out) {
    auto success = _db.get()->getKeysWithResult(keys_out, results_out, error_out);
    std::lock_guard<std::mutex> guard(keyCacheMutex);
    buildUpKeyCache(keys_out);
    return success;
  }
};

const llb_data_t mapData(std::vector<uint8_t> input) {
  const auto size = sizeof(input.front()) * input.size();
  const auto newData = (uint8_t *)malloc(size);
  memcpy(newData, input.data(), size);
  
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
  for (auto keyIDAndFlag: result.dependencies) {
    std::string error;
    auto buildKey = db.buildKeyForIdentifier(keyIDAndFlag.keyID, &error);
    if (!error.empty() || buildKey == nullptr) {
      assert(0 && "Got dependency from database and don't know this identifier.");
      continue;
    }
    deps[index] = (llb_build_key_t *)buildKey;
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

class CAPIBuildDBFetchResult {
private:
  const std::vector<KeyID> keys;
  std::vector<llb_database_result_t> results;
  CAPIBuildDB &db;
  
public:
  const bool hasResults;
  
  CAPIBuildDBFetchResult(const std::vector<KeyID>& keys, CAPIBuildDB& db): keys(keys), results({}), db(db), hasResults(false) {}
  CAPIBuildDBFetchResult(const std::vector<KeyID>& keys, const std::vector<llb_database_result_t> results, CAPIBuildDB &db): keys(keys), results(results), db(db), hasResults(true) {
    assert(keys.size() == results.size() && "The size of keys and results should be the same when initializing a database fetch result.");
  }
  
  CAPIBuildDBFetchResult(const CAPIBuildDBFetchResult&) LLBUILD_DELETED_FUNCTION;
  CAPIBuildDBFetchResult& operator=(const CAPIBuildDBFetchResult&) LLBUILD_DELETED_FUNCTION;
  
  size_t size() {
    return keys.size();
  }
  
  llb_build_key_t *keyAtIndex(int32_t index) {
    std::string error;
    auto buildKey = db.buildKeyForIdentifier(keys[index], &error);
    if (!error.empty() || buildKey == nullptr) {
      assert(0 && "Couldn't find build key for unknown identifier");
    }
    return (llb_build_key_t *)buildKey;
  }
  
  llb_database_result_t *resultAtIndex(int32_t index) {
    return &results[index];
  }
};

}

void llb_database_destroy_result(llb_database_result_t *result) {\
  llb_data_destroy(&result->value);
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

bool llb_database_lookup_rule_result(llb_database_t *database, llb_build_key_t *key, llb_database_result_t *result_out, llb_data_t *error_out) {
  
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

llb_database_key_id llb_database_fetch_result_get_count(llb_database_fetch_result_t *result) {
  auto resultKeys = (CAPIBuildDBFetchResult *)result;
  return resultKeys->size();
}

llb_build_key_t * llb_database_fetch_result_get_key_at_index(llb_database_fetch_result_t *result, int32_t index) {
  auto resultKeys = (CAPIBuildDBFetchResult *)result;
  return resultKeys->keyAtIndex(index);
}

bool llb_database_fetch_result_contains_rule_results(llb_database_fetch_result_t *result) {
  return ((CAPIBuildDBFetchResult *)result)->hasResults;
}

llb_database_result_t *llb_database_fetch_result_get_result_at_index(llb_database_fetch_result_t *result, int32_t index) {
  auto resultKeys = (CAPIBuildDBFetchResult *)result;
  // TODO: Check if results were fetched
  return resultKeys->resultAtIndex(index);
}

void llb_database_destroy_fetch_result(llb_database_fetch_result_t *result) {
  delete (CAPIBuildDBFetchResult *)result;
}

bool llb_database_get_keys(llb_database_t *database, llb_database_fetch_result_t **keysResult_out, llb_data_t *error_out) {
  auto db = (CAPIBuildDB *)database;
  
  std::vector<KeyType> keys;
  std::string error;
  
  auto success = db->getKeys(keys, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
    return success;
  }
  
  std::vector<KeyID> buildKeys;
  buildKeys.reserve(keys.size());
  for (auto rawKey: keys) {
    buildKeys.emplace_back(db->getKeyID(rawKey));
  }
  
  auto resultKeys = new CAPIBuildDBFetchResult(buildKeys, *db);
  *keysResult_out = (llb_database_fetch_result_t *)resultKeys;
  
  return success;
}

bool llb_database_get_keys_and_results(llb_database_t *database, llb_database_fetch_result_t *_Nullable *_Nonnull keysAndResults_out, llb_data_t *_Nullable error_out) {
  
  auto db = (CAPIBuildDB *)database;
  
  std::vector<KeyType> keys;
  std::vector<Result> results;
  std::string error;
  
  auto success = db->getKeysWithResult(keys, results, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t *)strdup(error.c_str());
    return success;
  }
  assert(keys.size() == results.size());
  
  std::vector<KeyID> keyIDs;
  keyIDs.reserve(keys.size());
  std::vector<llb_database_result_t> databaseResults;
  databaseResults.reserve(results.size());
  
  for (size_t index = 0; index < keys.size(); index++) {
    keyIDs.emplace_back(db->getKeyID(keys[index]));
    databaseResults.emplace_back(mapResult(*db, results[index]));
  }
  
  auto keysAndResults = new CAPIBuildDBFetchResult(keyIDs, databaseResults, *db);
  *keysAndResults_out = (llb_database_fetch_result_t *)keysAndResults;
  return success;
}

