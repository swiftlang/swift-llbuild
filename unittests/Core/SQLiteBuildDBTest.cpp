//===- unittests/Core/SQLiteBuildDBTest.cpp -----------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Core/BuildDB.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"

#include "gtest/gtest.h"

#include <sstream>

#include <sqlite3.h>

using namespace llbuild;
using namespace llbuild::core;

TEST(SQLiteBuildDBTest, ErrorHandling) {
    // Create a temporary file.
    llvm::SmallString<256> dbPath;
    auto ec = llvm::sys::fs::createTemporaryFile("build", "db", dbPath);
    EXPECT_EQ(bool(ec), false);
    const char* path = dbPath.c_str();
    fprintf(stderr, "using db: %s\n", path);

    std::string error;
    std::unique_ptr<BuildDB> buildDB = createSQLiteBuildDB(dbPath, 1, &error);
    EXPECT_TRUE(buildDB != nullptr);
    EXPECT_EQ(error, "");

    sqlite3 *db = nullptr;
    sqlite3_open(path, &db);
    sqlite3_exec(db, "PRAGMA locking_mode = EXCLUSIVE; BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr);

    buildDB = createSQLiteBuildDB(dbPath, 1, &error);
    EXPECT_TRUE(buildDB == nullptr);
    std::stringstream out;
    out << "error: accessing build database \"" << path << "\": database is locked Possibly there are two concurrent builds running in the same filesystem location.";
    EXPECT_EQ(error, out.str());

    ec = llvm::sys::fs::remove(dbPath.str());
    EXPECT_EQ(bool(ec), false);
}

TEST(SQLiteBuildDBTest, LockedWhileBuilding) {
  // Create a temporary file.
  llvm::SmallString<256> dbPath;
  auto ec = llvm::sys::fs::createTemporaryFile("build", "db", dbPath);
  EXPECT_EQ(bool(ec), false);
  const char* path = dbPath.c_str();
  fprintf(stderr, "using db: %s\n", path);

  std::string error;
  std::unique_ptr<BuildDB> buildDB = createSQLiteBuildDB(dbPath, 1, &error);
  EXPECT_TRUE(buildDB != nullptr);
  EXPECT_EQ(error, "");

  std::unique_ptr<BuildDB> secondBuildDB = createSQLiteBuildDB(dbPath, 1, &error);
  EXPECT_TRUE(buildDB != nullptr);
  EXPECT_EQ(error, "");

  bool result = buildDB->buildStarted(&error);
  EXPECT_TRUE(result);
  EXPECT_EQ(error, "");

  // Tests that we cannot start a second build with an existing connection
  result = secondBuildDB->buildStarted(&error);
  EXPECT_FALSE(result);
  std::stringstream out;
  out << "error: accessing build database \"" << path << "\": database is locked Possibly there are two concurrent builds running in the same filesystem location.";
  EXPECT_EQ(error, out.str());

  // Tests that we cannot create new connections while a build is running
  std::unique_ptr<BuildDB> otherBuildDB = createSQLiteBuildDB(dbPath, 1, &error);
  EXPECT_TRUE(otherBuildDB == nullptr);
  EXPECT_EQ(error, out.str());

  ec = llvm::sys::fs::remove(dbPath.str());
  EXPECT_EQ(bool(ec), false);
}
