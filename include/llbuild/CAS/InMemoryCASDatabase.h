//===- InMemoryCASDatabase.h ------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CAS_INMEMORYCASDATABASE_H
#define LLBUILD_CAS_INMEMORYCASDATABASE_H

#include "llbuild/CAS/CASDatabase.h"

#include "llvm/ADT/DenseMap.h"

#include <atomic>

namespace llbuild {
namespace CAS {

/// An in-memory CAS database.
class InMemoryCASDatabase: public CASDatabase {
  /// The objects in the database.
  llvm::DenseMap<DataID, CASObject> objects;

  /// The lock protecting concurrent access;
  std::mutex mutex;
  
public:
  InMemoryCASDatabase() {}

  /// Check if the database contains the given.
  virtual auto contains(const DataID& id) ->
    std::future<llvm::ErrorOr<bool>>;

  /// Get the given object, if present.
  virtual auto get(const DataID& id) ->
    std::future<llvm::ErrorOr<std::unique_ptr<CASObject>>>;

  /// Write the given object into the database.
  virtual auto putObject(std::unique_ptr<CASObject> object) ->
    std::future<llvm::ErrorOr<DataID>>;
};

}
}

#endif
