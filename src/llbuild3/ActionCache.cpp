//===-- ActionCache.cpp ---------------------------------------------------===//
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

#include "llbuild3/ActionCache.h"

using namespace llbuild3;

ActionCache::~ActionCache() { }

InMemoryActionCache::~InMemoryActionCache() { }

void InMemoryActionCache::get(const CacheKey& key, std::function<void(result<CacheValue, Error>)> resultHandler) {
  CacheValue value;

  {
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (auto entry = cache.find(key.content().bytes()); entry != cache.end()) {
      value = entry->second;
    }
  }

  resultHandler(value);
}

void InMemoryActionCache::update(const CacheKey& key, const CacheValue& value) {
  std::lock_guard<std::mutex> lock(cacheMutex);
  cache.insert_or_assign(key.content().bytes(), value);
}

