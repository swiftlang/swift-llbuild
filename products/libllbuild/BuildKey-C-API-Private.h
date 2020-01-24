//===-- BuildKey-C-API-Private.h ------------------------------------------===//
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
    hashValue = std::hash<llbuild::core::KeyType>{}(internalBuildKey.getKeyData());
  }
  
  CAPIBuildKey(const BuildKey &buildKey, llbuild::core::KeyID identifier): identifier(identifier), hasIdentifier(true), internalBuildKey(buildKey) {
    hashValue = std::hash<llbuild::core::KeyType>{}(internalBuildKey.getKeyData());
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
