//===- CASDatabase.h --------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_CAS_CASDATABASE_H
#define LLBUILD_CAS_CASDATABASE_H

#include "llbuild/CAS/DataID.h"

#include "llbuild/Basic/Compiler.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"

#include <cstdint>
#include <memory>
#include <future>

namespace llbuild {
namespace CAS {

struct CASObject;
  
/// An abstract content-addressable storage (CAS) database.
class CASDatabase {
private:
  // Copying is disabled.
  CASDatabase(const CASDatabase&) LLBUILD_DELETED_FUNCTION;
  void operator=(const CASDatabase&) LLBUILD_DELETED_FUNCTION;

public:
  CASDatabase() {}
  virtual ~CASDatabase();

  /// Check if the database contains the given.
  virtual auto contains(const DataID& id) ->
    std::future<llvm::ErrorOr<bool>> = 0;

  /// Get the given object, if present.
  virtual auto get(const DataID& id) ->
    std::future<llvm::ErrorOr<std::unique_ptr<CASObject>>> = 0;

  /// Write the given object into the database.
  virtual auto putObject(std::unique_ptr<CASObject> object) ->
    std::future<llvm::ErrorOr<DataID>> = 0;

  /// Write an object into the database.
  inline auto put(std::unique_ptr<CASObject> object)
    -> std::future<llvm::ErrorOr<DataID>>
  {
    return putObject(std::move(object));
  }

  /// Write a data-only object into the database.
  auto put(llvm::StringRef data) -> std::future<llvm::ErrorOr<DataID>>;
};

/// An individual object in the CAS.
struct CASObject {
public:
  /// The list of references in this object.
  llvm::SmallVector<DataID, 8> refs;

  /// The data in this object.
  llvm::SmallVector<uint8_t, 256> data;
};

}
}

#endif
