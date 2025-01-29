//===- CAS.h ----------------------------------------------------*- C++ -*-===//
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

#include "llbuild3/CAS.h"

#include <llbuild3/Errors.hpp>

#include "blake3.h"

using namespace llbuild3;

CASDatabase::~CASDatabase() { }

void* CASDatabase::__raw_context() { return nullptr; }

InMemoryCASDatabase::~InMemoryCASDatabase() { }

void InMemoryCASDatabase::contains(const CASObjectID& casid, std::function<void(result<bool, Error>)> resultHandler) {
  bool found = false;

  {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (auto entry = db.find(casid.bytes()); entry != db.end()) {
      found = true;
    }
  }

  resultHandler(found);
}

void InMemoryCASDatabase::get(const CASObjectID& casid, std::function<void(result<CASObject, Error>)> resultHandler) {
  CASObject value;

  {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (auto entry = db.find(casid.bytes()); entry != db.end()) {
      value = entry->second;
    }
  }

  resultHandler(value);
}

namespace {
  void calcIDForObject(CASObjectID& casid, const CASObject& object) {
    blake3_hasher hasher;

    blake3_hasher_init(&hasher);

    for (auto ref : object.refs()) {
      blake3_hasher_update(&hasher, ref.bytes().data(), ref.bytes().length());
    }

    blake3_hasher_update(&hasher, object.data().data(), object.data().length());

    std::array<uint8_t, BLAKE3_OUT_LEN> buffer;
    blake3_hasher_finalize(&hasher, buffer.data(), buffer.size());

    casid.mutable_bytes()->assign(std::begin(buffer), std::end(buffer));
  }
}

void InMemoryCASDatabase::put(const CASObject& object, std::function<void(result<CASObjectID, Error>)> resultHandler) {
  CASObjectID casid;
  calcIDForObject(casid, object);

  {
    std::lock_guard<std::mutex> lock(dbMutex);
    db.insert_or_assign(casid.bytes(), object);
  }

  resultHandler(casid);
}

CASObjectID InMemoryCASDatabase::identify(const CASObject& object) {
  CASObjectID casid;
  calcIDForObject(casid, object);
  return casid;
}
