//
//  BuildKey-C-API-Private.h
//  llbuild
//
//  Created by Benjamin Herzog on 01.08.19.
//  Copyright © 2019 Apple Inc. All rights reserved.
//

#ifndef BuildKey_C_API_Private_h
#define BuildKey_C_API_Private_h

#include "llbuild/BuildSystem/BuildKey.h"
#include "llbuild/Core/BuildEngine.h"

namespace {
using namespace llbuild::buildsystem;

/// This class is used as a context pointer in the client
class CAPIBuildKey {
  
private:
  llbuild::core::KeyID identifier;
  bool hasIdentifier;
  BuildKey internalBuildKey;
  size_t hashValue;
public:
  
  CAPIBuildKey(const BuildKey &buildKey): hasIdentifier(false), internalBuildKey(buildKey) {
    hashValue = std::hash<std::string>{}(internalBuildKey.getKeyData());
  }
  
  CAPIBuildKey(const BuildKey &buildKey, llbuild::core::KeyID identifier): identifier(identifier), hasIdentifier(true), internalBuildKey(buildKey) {
    hashValue = std::hash<std::string>{}(internalBuildKey.getKeyData());
  }
  
  BuildKey &getInternalBuildKey() {
    return internalBuildKey;
  }
  size_t getHashValue() {
    return hashValue;
  }
  llb_build_key_kind_t getKind();
  
  bool operator ==(const CAPIBuildKey &b);
};
}

namespace std {
template <>
struct hash<CAPIBuildKey> {
  size_t operator()(CAPIBuildKey& key) const;
};
}

#endif /* BuildKey_C_API_Private_h */
