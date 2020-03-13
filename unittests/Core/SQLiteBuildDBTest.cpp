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

#include "llbuild/Core/BuildEngine.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"

#include "gtest/gtest.h"

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
    std::unique_ptr<BuildDB> buildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
    EXPECT_TRUE(buildDB != nullptr);
    EXPECT_EQ(error, "");

    sqlite3 *db = nullptr;
    sqlite3_open(path, &db);
    sqlite3_exec(db, "PRAGMA locking_mode = EXCLUSIVE; BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr);

    buildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
    EXPECT_FALSE(buildDB == nullptr);

    // The database is opened lazily, thus run an operation that will cause it
    // to be opened and verify that it fails as expected.
    bool result = true;
    buildDB->getCurrentEpoch(&result, &error);
    EXPECT_FALSE(result);

    std::stringstream out;
    out << "error: accessing build database \"" << path << "\": database is locked Possibly there are two concurrent builds running in the same filesystem location.";
    EXPECT_EQ(error, out.str());

    // Clean up database connections before unlinking
    sqlite3_exec(db, "END;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    buildDB = nullptr;

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
  std::unique_ptr<BuildDB> buildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
  EXPECT_TRUE(buildDB != nullptr);
  EXPECT_EQ(error, "");

  std::unique_ptr<BuildDB> secondBuildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
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
  std::unique_ptr<BuildDB> otherBuildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
  EXPECT_FALSE(otherBuildDB == nullptr);

  // The database is opened lazily, thus run an operation that will cause it
  // to be opened and verify that it fails as expected.
  bool success = true;
  otherBuildDB->getCurrentEpoch(&success, &error);
  EXPECT_FALSE(success);
  EXPECT_EQ(error, out.str());

  // Clean up database connections before unlinking
  buildDB->buildComplete();
  buildDB = nullptr;
  secondBuildDB = nullptr;
  otherBuildDB = nullptr;

  ec = llvm::sys::fs::remove(dbPath.str());
  EXPECT_EQ(bool(ec), false);
}

TEST(SQLiteBuildDBTest, CloseDBConnectionAfterCloseCall) {
  // Create a temporary file.
  llvm::SmallString<256> dbPath;
  auto ec = llvm::sys::fs::createTemporaryFile("build", "db", dbPath);
  EXPECT_EQ(bool(ec), false);
  const char* path = dbPath.c_str();
  fprintf(stderr, "using db: %s\n", path);
  
  std::string error;
  std::unique_ptr<BuildDB> buildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
  EXPECT_TRUE(buildDB != nullptr);
  EXPECT_EQ(error, "");
  
  buildDB->buildStarted(&error);
  EXPECT_EQ(error, "");
  
  buildDB->buildComplete();
}


TEST(SQLiteBuildDBTest, DoubleEpoch) {
  // Test setting the current iteration to the same value.
  // Not necessarily a good thing, but also not explicitly disallowed either.
  // This was a disproven theory for how we might get unexpected error codes
  // from SQLite. Given that it works, leaving it in for the coverage it does
  // provide.  Also, maybe we should change the interface to disallow it in the
  // future?

  // Create a temporary file.
  llvm::SmallString<256> dbPath;
  auto ec = llvm::sys::fs::createTemporaryFile("build", "db", dbPath);
  EXPECT_EQ(bool(ec), false);
  const char* path = dbPath.c_str();
  fprintf(stderr, "using db: %s\n", path);

  std::string error;
  std::unique_ptr<BuildDB> buildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
  EXPECT_TRUE(buildDB != nullptr);
  EXPECT_EQ(error, "");

  buildDB->buildStarted(&error);
  EXPECT_EQ(error, "");
  buildDB->setCurrentIteration(1, &error);
  EXPECT_EQ(error, "");
  buildDB->buildComplete();

  buildDB->buildStarted(&error);
  EXPECT_EQ(error, "");
  buildDB->setCurrentIteration(1, &error);
  EXPECT_EQ(error, "");
  buildDB->buildComplete();
}

TEST(SQLiteBuildDBTest, DoubleSetResult) {
  // Test that setting a rule result to the exact same values works.

  // Create a temporary file.
  llvm::SmallString<256> dbPath;
  auto ec = llvm::sys::fs::createTemporaryFile("build", "db", dbPath);
  EXPECT_EQ(bool(ec), false);
  const char* path = dbPath.c_str();
  fprintf(stderr, "using db: %s\n", path);

  std::string error;
  std::unique_ptr<BuildDB> buildDB = createSQLiteBuildDB(dbPath, 1, /* recreateUnmatchedVersion = */ true, &error);
  EXPECT_TRUE(buildDB != nullptr);
  EXPECT_EQ(error, "");

  class TestRule: public Rule, public BuildDBDelegate {
  public:
      TestRule(const KeyType& key) : Rule(key) { }

      Task* createTask(BuildEngine&) override { return nullptr; }
      bool isResultValid(BuildEngine&, const ValueType& value) override {
          return true;
      }

    const KeyID getKeyID(const KeyType& key) override { return KeyID(this); }
    KeyType getKeyForID(const KeyID key) override { return "rule"; }
  };


  TestRule rule("rule");
  buildDB->attachDelegate(&rule);

  buildDB->buildStarted(&error);
  EXPECT_EQ(error, "");
  Result result;
  result.computedAt = 1;
  buildDB->setRuleResult(KeyID(&rule), rule, result, &error);
  EXPECT_EQ(error, "");
  buildDB->setRuleResult(KeyID(&rule), rule, result, &error);
  EXPECT_EQ(error, "");
  buildDB->buildComplete();

}
