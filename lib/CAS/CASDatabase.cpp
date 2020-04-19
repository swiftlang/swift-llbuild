//===-- CASDatabase.cpp ---------------------------------------------------===//
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

#include "llbuild/CAS/CASDatabase.h"

using namespace llbuild;
using namespace llbuild::CAS;

CASDatabase::~CASDatabase() {}

auto CASDatabase::put(llvm::StringRef data) ->
  std::future<llvm::ErrorOr<DataID>>
{
  auto object = llvm::make_unique<CASObject>();
  object->data.insert(object->data.begin(), data.begin(), data.end());
  return put(std::move(object));
}
