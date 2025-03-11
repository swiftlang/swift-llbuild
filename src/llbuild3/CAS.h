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

#ifndef LLBUILD3_CAS_H
#define LLBUILD3_CAS_H

#include <llbuild3/Result.hpp>

#include "llbuild3/Error.pb.h"
#include "llbuild3/CAS.pb.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace llbuild3 {

class CASDatabase {
public:
  virtual ~CASDatabase();

  virtual void contains(const CASID& casid, std::function<void(result<bool, Error>)> resultHandler) = 0;

  virtual void get(const CASID& casid, std::function<void(result<CASObject, Error>)> resultHandler) = 0;

  virtual void put(const CASObject& object, std::function<void(result<CASID, Error>)> resultHandler) = 0;

  virtual CASID identify(const CASObject& object) = 0;

  virtual void* __raw_context();
};

class InMemoryCASDatabase: public CASDatabase {
private:
  std::unordered_map<std::string, CASObject> db;
  std::mutex dbMutex;

public:
  InMemoryCASDatabase() { }
  ~InMemoryCASDatabase();

  void contains(const CASID& casid, std::function<void(result<bool, Error>)> resultHandler);

  void get(const CASID& casid, std::function<void(result<CASObject, Error>)> resultHandler);

  void put(const CASObject& object, std::function<void(result<CASID, Error>)> resultHandler);

  CASID identify(const CASObject& object);
};

std::string CASIDAsCanonicalString(const CASID& objID);
void ParseCanonicalCASIDString(CASID& objID, const std::string& str);

}

#endif
