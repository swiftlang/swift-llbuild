//===- InMemoryCASDatabaseTests.cpp ---------------------------------------===//
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

#include "gtest/gtest.h"

using namespace llbuild;
using namespace llbuild::CAS;

TEST(InMemoryCASDatabaseTests, basic) {
  InMemoryCASDatabase db;

  // Put an trivial object.
  auto id = db.put("X").get().get();
  auto id2 = db.put("X").get().get();

  // Validate we get the same result for each put.
  EXPECT_EQ(id, id2);

  // Validate we can retrieve the object.
  auto object = std::move(db.get(id).get().get());
  EXPECT_EQ(object->data.size(), 1U);
  EXPECT_EQ(object->data[0], 'X');

  // A different object must be different.
  auto id3 = db.put("Y").get().get();
  EXPECT_NE(id3, id);
}
