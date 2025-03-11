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

#include "absl/strings/escaping.h"

#include "blake3.h"

using namespace llbuild3;

CASDatabase::~CASDatabase() { }

void* CASDatabase::__raw_context() { return nullptr; }

InMemoryCASDatabase::~InMemoryCASDatabase() { }

void InMemoryCASDatabase::contains(const CASID& casid, std::function<void(result<bool, Error>)> resultHandler) {
  bool found = false;

  {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (auto entry = db.find(casid.bytes()); entry != db.end()) {
      found = true;
    }
  }

  resultHandler(found);
}

void InMemoryCASDatabase::get(const CASID& casid, std::function<void(result<CASObject, Error>)> resultHandler) {
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
  void calcIDForObject(CASID& casid, const CASObject& object) {
    blake3_hasher hasher;

    blake3_hasher_init(&hasher);

    for (auto ref : object.refs()) {
      blake3_hasher_update(&hasher, ref.bytes().data(), ref.bytes().length());
    }

    blake3_hasher_update(&hasher, object.data().data(), object.data().length());

    std::array<uint8_t, BLAKE3_OUT_LEN + 1> buffer;
    buffer[0] = 5; // id type byte
    blake3_hasher_finalize(&hasher, buffer.data() + 1, buffer.size() - 1);

    casid.mutable_bytes()->assign(std::begin(buffer), std::end(buffer));
  }
}

void InMemoryCASDatabase::put(const CASObject& object, std::function<void(result<CASID, Error>)> resultHandler) {
  CASID casid;
  calcIDForObject(casid, object);

  {
    std::lock_guard<std::mutex> lock(dbMutex);
    db.insert_or_assign(casid.bytes(), object);
  }

  resultHandler(casid);
}

CASID InMemoryCASDatabase::identify(const CASObject& object) {
  CASID casid;
  calcIDForObject(casid, object);
  return casid;
}

std::string llbuild3::CASIDAsCanonicalString(const CASID& objID) {
  if (objID.bytes().size() == 0) {
    return {};
  }

  std::string str;
  str.push_back('0' + objID.bytes()[0]);
  str.push_back('~');
  std::string_view hashBytes(objID.bytes().begin() + 1, objID.bytes().end());
  str.append(absl::WebSafeBase64Escape(hashBytes));

  return str;
}

void llbuild3::ParseCanonicalCASIDString(CASID& objID, const std::string& str) {
  if (str.size() < 2 || str[1] != '~') {
    return;
  }

  std::string_view hashBytes(str.begin() + 2, str.end());
  std::string decoded;
  if (!absl::WebSafeBase64Unescape(hashBytes, &decoded)) {
    return;
  }

  auto& dest = *objID.mutable_bytes();
  dest.push_back(str[0] - '0');
  dest.append(decoded);
}
