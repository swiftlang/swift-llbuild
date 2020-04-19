//===-- InMemoryCASDatabase.cpp -------------------------------------------===//
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

#include "llbuild/CAS/InMemoryCASDatabase.h"

#include "llbuild/Basic/Defer.h"

using namespace llbuild;
using namespace llbuild::CAS;

auto InMemoryCASDatabase::contains(const DataID& id) ->
  std::future<llvm::ErrorOr<bool>>
{
  std::unique_lock<std::mutex> lock(mutex);

  std::promise<llvm::ErrorOr<bool>> p;
  p.set_value(objects.count(id) != 0);

  return p.get_future();
}

auto InMemoryCASDatabase::get(const DataID& id) ->
  std::future<llvm::ErrorOr<std::unique_ptr<CASObject>>>
{
  std::unique_lock<std::mutex> lock(mutex);

  std::promise<llvm::ErrorOr<std::unique_ptr<CASObject>>> p;
  const auto& it = objects.find(id);
  if (it == objects.end()) {
    p.set_value(nullptr);
  } else {
    // Copy the object.
    p.set_value(llvm::make_unique<CASObject>(
                    CASObject{ it->second.refs, it->second.data }));
  }

  return p.get_future();
}

auto InMemoryCASDatabase::putObject(std::unique_ptr<CASObject> object) ->
  std::future<llvm::ErrorOr<DataID>>
{
  std::unique_lock<std::mutex> lock(mutex);

  // FIXME: Actually hash the object.
  DataID hash("somehash");
  // Steal the object.
  objects.insert(std::make_pair(hash, *object.release()));
  
  std::promise<llvm::ErrorOr<DataID>> p;
  p.set_value(hash);
  return p.get_future();
}
