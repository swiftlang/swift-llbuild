//===-- Basic-C-API.cpp ---------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2015 - 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

// Include the public API.
#include <llbuild/llbuild.h>

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#if !defined(__APPLE__) || !TARGET_OS_IPHONE

#include "llbuild/Basic/Defer.h"
#include "llbuild/Basic/FileSystem.h"
#include "llbuild/Core/BuildEngine.h"

#include "llvm/Support/SourceMgr.h"

using namespace llbuild::basic;

/* Basic API */

llb_quality_of_service_t llb_get_quality_of_service() {
  switch (getDefaultQualityOfService()) {
  case QualityOfService::Normal:
    return llb_quality_of_service_default;
  case QualityOfService::UserInitiated:
    return llb_quality_of_service_user_initiated;
  case QualityOfService::Utility:
    return llb_quality_of_service_utility;
  case QualityOfService::Background:
    return llb_quality_of_service_background;
  default:
    assert(0 && "unknown quality service level");
    return llb_quality_of_service_default;
  }
}

void llb_set_quality_of_service(llb_quality_of_service_t level) {
  switch (level) {
  case llb_quality_of_service_default:
    setDefaultQualityOfService(QualityOfService::Normal);
    break;
  case llb_quality_of_service_user_initiated:
    setDefaultQualityOfService(QualityOfService::UserInitiated);
    break;
  case llb_quality_of_service_utility:
    setDefaultQualityOfService(QualityOfService::Utility);
    break;
  case llb_quality_of_service_background:
    setDefaultQualityOfService(QualityOfService::Background);
    break;
  default:
    assert(0 && "unknown quality service level");
    break;
  }
}

void* llb_alloc(size_t size) { return malloc(size); }
void llb_free(void* ptr) { free(ptr); }

#endif
