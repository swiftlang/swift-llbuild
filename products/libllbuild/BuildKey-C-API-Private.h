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

namespace {
using namespace llbuild::buildsystem;

/// This class is used as a context pointer in the client
class CAPIBuildKey {
  
private:
  BuildKey internalBuildKey;
public:
  
  CAPIBuildKey(const BuildKey &buildKey): internalBuildKey(buildKey) {}
  
  BuildKey &getInternalBuildKey() {
    return internalBuildKey;
  }
  llb_build_key_kind_t getKind();
};
}

#endif /* BuildKey_C_API_Private_h */
