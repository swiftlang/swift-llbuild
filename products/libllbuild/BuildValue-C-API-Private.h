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

#ifndef BuildValue_C_API_Private_h
#define BuildValue_C_API_Private_h

#include "llbuild/BuildSystem/BuildValue.h"

namespace {
using namespace llbuild::buildsystem;

/// This class is used as a context pointer in the client
class CAPIBuildValue {

private:
  const BuildValue internalBuildValue;
public:

  CAPIBuildValue(BuildValue buildValue): internalBuildValue(std::move(buildValue)) {}

  const BuildValue &getInternalBuildValue() {
    return internalBuildValue;
  }

  llb_build_value_kind_t getKind();
};
}

namespace llbuild {
namespace capi {

const llb_build_value_file_info_t convertFileInfo(const llbuild::basic::FileInfo &fileInfo);

}
}

#endif /* BuildValue_C_API_Private_h */
