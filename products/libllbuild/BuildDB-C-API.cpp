//
//  BuildDB-C-API.c
//  libllbuild
//
//  Created by Benjamin Herzog on 4/5/19.
//  Copyright © 2019 Apple Inc. All rights reserved.
//

#include <llbuild/llbuild.h>

#include "llbuild/Core/BuildDB.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <memory>

using namespace llbuild;
using namespace llbuild::core;

namespace {

class CAPIBuildDBResultKeys {
private:
  std::unique_ptr<std::map<KeyID, KeyType>> keys;
  std::unique_ptr<std::map<KeyType, KeyID>> ids;
  
public:
  CAPIBuildDBResultKeys(std::map<KeyID, KeyType> keys): keys(llvm::make_unique<std::map<KeyID, KeyType>>(keys)) {
    ids = llvm::make_unique<std::map<KeyType, KeyID>>();
    for (const auto &pair : keys) {
      (*ids.get())[std::get<1>(pair)] = std::get<0>(pair);
    }
  }
  
  uint32_t size() {
    return keys.get()->size();
  }
  
  KeyType keyForID(KeyID index) {
    return (*keys.get())[index];
  }
  
  KeyID idForKey(KeyType key) {
    return (*ids.get())[key];
  }
};
  
class CAPIBuildDBDelegate: public BuildDBDelegate {
  llb_database_delegate_t cAPIDelegate;
  
public:
  CAPIBuildDBDelegate(llb_database_delegate_t delegate): cAPIDelegate(delegate) { }
  
  KeyType getKeyForID(KeyID keyID) override {
    if (!cAPIDelegate.get_key_for_id) {
      return nullptr;
    }
    
    llb_database_key_type key;
    cAPIDelegate.get_key_for_id(cAPIDelegate.context, keyID, &key);
    
    return std::string(key);
  }
  
  KeyID getKeyID(const KeyType &key) override {
    if (!cAPIDelegate.get_key_id) {
      return 0;
    }
    
    return cAPIDelegate.get_key_id(cAPIDelegate.context, (char *)key.c_str());
  }
  
  llb_database_delegate_t getCAPIDelegate() {
    return cAPIDelegate;
  }
};
  
class CAPIBuildDB {
  CAPIBuildDBDelegate cAPIDelegate;
  
  std::unique_ptr<BuildDB> _db;
  
public:
  CAPIBuildDB(StringRef path, uint32_t clientSchemaVersion, CAPIBuildDBDelegate delegate, std::string *error_out): cAPIDelegate(delegate) {
    _db = createSQLiteBuildDB(path, clientSchemaVersion, error_out);
    _db.get()->attachDelegate(&cAPIDelegate);
  }
  
  uint64_t getCurrentIteration(bool *success_out, std::string *error_out) {
    return _db.get()->getCurrentIteration(success_out, error_out);
  }
  
  void setCurrentIteration(uint64_t value, std::string *error_out) {
    _db.get()->setCurrentIteration(value, error_out);
  }
  
  bool lookupRuleResult(KeyID keyID, KeyType ruleKey, Result *result_out, std::string *error_out) {
    return _db.get()->lookupRuleResult(keyID, ruleKey, result_out, error_out);
  }
  
  bool buildStarted(std::string *error_out) {
    return _db.get()->buildStarted(error_out);
  }
  
  void buildComplete() {
    _db.get()->buildComplete();
  }
  
  bool getKeys(std::map<KeyID, KeyType>& keys_out, std::string *error_out) {
    return _db.get()->getKeys(keys_out, error_out);
  }
  
  void dump() {
    _db.get()->dump(llvm::outs());
  }
};

}

llb_data_t mapData(std::vector<uint8_t> input) {
  const auto data = input.data();
  const auto size = sizeof(data) * input.size();
  const auto newData = (uint8_t *)malloc(size);
  memcpy(newData, data, size);
  
  return llb_data_t {
    input.size(),
    newData
  };
}

llb_database_result_t mapResult(Result result) {
  
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

llb_database_t* llb_database_create(
                                    char *path,
                                    uint32_t clientSchemaVersion,
                                    llb_database_delegate_t delegate,
                                    llb_data_t *error_out) {
  assert(delegate.get_key_for_id);
  assert(delegate.get_key_id);
  
  std::string error;
  
  auto database = new CAPIBuildDB(StringRef(path), clientSchemaVersion, delegate, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
  
  return (llb_database_t *)database;
}

void llb_database_destroy(llb_database_t *database) {
  delete (CAPIBuildDB *)database;
}

uint64_t llb_database_get_current_iteration(llb_database_t *database, bool *success_out, llb_data_t *error_out) {
  auto db = (CAPIBuildDB *)database;
  std::string error;
  
  auto iteration = db->getCurrentIteration(success_out, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
  
  return iteration;
}

void llb_database_set_current_iteration(llb_database_t *database, uint64_t value, llb_data_t *error_out) {
  auto db = (CAPIBuildDB *)database;
  std::string error;
  
  db->setCurrentIteration(value, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
}

bool llb_database_lookup_rule_result(llb_database_t *database, llb_database_key_id keyID, llb_database_key_type ruleKey, llb_database_result_t *result_out, llb_data_t *error_out) {
  
  auto db = (CAPIBuildDB *)database;
  
  std::string error;
  Result result;
  auto stored = db->lookupRuleResult(keyID, KeyType(ruleKey), &result, &error);
  
  if (result_out) {
    *result_out = mapResult(result);
  }
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
  }
  
  return stored;
}

bool llb_database_build_started(llb_database_t *database, llb_data_t *error_out) {
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

llb_database_key_id llb_database_result_keys_get_count(llb_database_result_keys_t *result) {
  auto resultKeys = (CAPIBuildDBResultKeys *)result;
  return resultKeys->size();
}

void llb_database_result_keys_get_key_for_id(llb_database_result_keys_t *result, llb_database_key_id keyID, llb_data_t *key_out) {
  auto resultKeys = (CAPIBuildDBResultKeys *)result;
  auto key = resultKeys->keyForID(keyID);
  key_out->length = key.size();
  key_out->data = (const uint8_t*) strdup(key.data());
}

llb_database_key_id llb_database_result_keys_get_id_for_key(llb_database_result_keys_t *result, llb_database_key_type key) {
  auto resultKeys = (CAPIBuildDBResultKeys *)result;
  return resultKeys->idForKey(std::string(key));
}

void llb_database_destroy_result_keys(llb_database_result_keys_t *result) {
  delete (CAPIBuildDBResultKeys *)result;
}

bool llb_database_get_keys(llb_database_t *database, llb_database_result_keys_t **keysResult_out, llb_data_t *error_out) {
  auto db = (CAPIBuildDB *)database;
  
  std::map<KeyID, KeyType> keys;
  std::string error;
  
  auto success = db->getKeys(keys, &error);
  
  if (!error.empty() && error_out) {
    error_out->length = error.size();
    error_out->data = (const uint8_t*)strdup(error.c_str());
    return success;
  }
  
  auto resultKeys = new CAPIBuildDBResultKeys(keys);
  *keysResult_out = (llb_database_result_keys_t *)resultKeys;
  
  return success;
}

void llb_database_dump(llb_database_t *database) {
  ((CAPIBuildDB *)database)->dump();
}
