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

#ifndef LLBUILD3_CORE_CAS_H
#define LLBUILD3_CORE_CAS_H

#include <llbuild3/Result.hpp>

#include "llbuild3/Error.pb.h"
#include "llbuild3/core/CAS.pb.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace llbuild3 {
namespace core {

class CASDatabase {
public:
  virtual ~CASDatabase();

  virtual void contains(const CASObjectID& casid, std::function<void(result<bool, Error>)> resultHandler) = 0;

  virtual void get(const CASObjectID& casid, std::function<void(result<CASObject, Error>)> resultHandler) = 0;

  virtual void put(const CASObject& object, std::function<void(result<CASObjectID, Error>)> resultHandler) = 0;

  virtual CASObjectID identify(const CASObject& object) = 0;
};

class InMemoryCASDatabase: public CASDatabase {
private:
  std::unordered_map<std::string, CASObject> db;
  std::mutex dbMutex;

public:
  InMemoryCASDatabase() { }
  ~InMemoryCASDatabase();

  void contains(const CASObjectID& casid, std::function<void(result<bool, Error>)> resultHandler);

  void get(const CASObjectID& casid, std::function<void(result<CASObject, Error>)> resultHandler);

  void put(const CASObject& object, std::function<void(result<CASObjectID, Error>)> resultHandler);

  CASObjectID identify(const CASObject& object);
};

}
}

#endif
