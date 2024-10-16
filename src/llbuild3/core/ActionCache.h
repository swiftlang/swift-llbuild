//===- ActionCache.h --------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2024 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_CORE_ACTIONCACHE_H
#define LLBUILD3_CORE_ACTIONCACHE_H

#include <llbuild3/Result.hpp>

#include "llbuild3/Error.pb.h"
#include "llbuild3/core/ActionCache.pb.h"
#include "llbuild3/core/CAS.pb.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace llbuild3 {
namespace core {

class ActionCache {
public:
  virtual ~ActionCache();

  virtual void get(const CacheKey& key, std::function<void(result<CacheValue, Error>)> resultHandler) = 0;

  virtual void update(const CacheKey& key, const CacheValue& value) = 0;
};

class InMemoryActionCache: public ActionCache {
private:
  std::unordered_map<std::string, CacheValue> cache;
  std::mutex cacheMutex;

public:
  InMemoryActionCache() { }
  ~InMemoryActionCache();

  void get(const CacheKey& key, std::function<void(result<CacheValue, Error>)> resultHandler);

  void update(const CacheKey& key, const CacheValue& value);
};

}
}

#endif
