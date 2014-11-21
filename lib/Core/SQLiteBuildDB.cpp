//===-- SQLiteBuildDB.cpp -------------------------------------------------===//
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

#include <cassert>
#include <cerrno>

#include <sqlite3.h>
#include <unistd.h>

using namespace llbuild;
using namespace llbuild::core;

// SQLite BuildDB Implementation

// FIXME: This entire implementation needs to be updated for good error
// handling.

namespace {

class SQLiteBuildDB : public BuildDB {
  const int CurrentSchemaVersion = 1;

  sqlite3 *DB = nullptr;

public:
  virtual ~SQLiteBuildDB() {
    if (DB)
      close();
  }

  bool open(const std::string& Path, std::string *Error_Out) {
    assert(!DB);
    int Result = sqlite3_open(Path.c_str(), &DB);
    if (Result != SQLITE_OK) {
      // FIXME: Provide better error messages.
      *Error_Out = "unable to open database";
      return false;
    }

    // Create the database schema, if necessary.
    char *CError;
    int Version;
    sqlite3_stmt* Stmt;
    Result = sqlite3_prepare_v2(
      DB, "SELECT version FROM info LIMIT 1",
      -1, &Stmt, nullptr);
    if (Result == SQLITE_ERROR) {
      Version = -1;
    } else {
      assert(Result == SQLITE_OK);

      Result = sqlite3_step(Stmt);
      if (Result == SQLITE_DONE) {
        Version = -1;
      } else if (Result == SQLITE_ROW) {
        assert(sqlite3_column_count(Stmt) == 1);
        Version = sqlite3_column_int(Stmt, 0);
      } else {
        abort();
      }
      sqlite3_finalize(Stmt);
    }

    if (Version != CurrentSchemaVersion) {
      // Always recreate the database from scratch when the schema changes.
      Result = unlink(Path.c_str());
      if (Result == -1) {
        if (errno != ENOENT) {
          *Error_Out = std::string("unable to unlink existing database: ") +
            ::strerror(errno);
          sqlite3_close(DB);
          return false;
        }
      } else {
        // If the remove was successful, reopen the database.
        int Result = sqlite3_open(Path.c_str(), &DB);
        if (Result != SQLITE_OK) {
          // FIXME: Provide better error messages.
          *Error_Out = "unable to open database";
          return false;
        }
      }

      // Create the info table.
      Result = sqlite3_exec(
        DB, ("CREATE TABLE info ("
             "id INTEGER PRIMARY KEY, "
             "version INTEGER, "
             "iteration INTEGER);"),
        nullptr, nullptr, &CError);
      if (Result == SQLITE_OK) {
        char* Query = sqlite3_mprintf(
          "INSERT INTO info VALUES (0, %d, 0);",
          CurrentSchemaVersion);
        Result = sqlite3_exec(DB, Query, nullptr, nullptr, &CError);
        free(Query);
      }
      if (Result == SQLITE_OK) {
        Result = sqlite3_exec(
          DB, ("CREATE TABLE rule_results ("
               "id INTEGER PRIMARY KEY, "
               "key STRING, "
               "value INTEGER, "
               "built_at INTEGER, "
               "computed_at INTEGER);"),
          nullptr, nullptr, &CError);
      }
      if (Result == SQLITE_OK) {
        Result = sqlite3_exec(
          DB, ("CREATE TABLE rule_dependencies ("
               "id INTEGER PRIMARY KEY, "
               "rule_id INTEGER, "
               "key STRING, "
               "FOREIGN KEY(rule_id) REFERENCES rule_info(id));"),
          nullptr, nullptr, &CError);
      }

      // Create the indices on the rule tables.
      if (Result == SQLITE_OK) {
        // Create an index to be used for efficiently looking up rule
        // information from a key.
        Result = sqlite3_exec(
          DB, "CREATE UNIQUE INDEX rule_results_idx ON rule_results (key);",
          nullptr, nullptr, &CError);
      }
      if (Result == SQLITE_OK) {
        // Create an index to be used for efficiently finding the dependencies
        // for a rule. This is a covering index.
        Result = sqlite3_exec(
          DB, ("CREATE INDEX rule_dependencies_idx ON "
               "rule_dependencies (rule_id, key);"),
          nullptr, nullptr, &CError);
      }
        
      if (Result != SQLITE_OK) {
        *Error_Out = (std::string("unable to initialize database (") + CError
                      + ")");
        ::free(CError);
        sqlite3_close(DB);
        return false;
      }
    }

    return true;
  }

  void close() {
    assert(DB);
    sqlite3_close(DB);
    DB = nullptr;
  }

  /// @name BuildDB API
  /// @{

  virtual uint64_t getCurrentIteration() override {
    assert(DB);

    // Fetch the iteration from the info table.
    sqlite3_stmt* Stmt;
    int Result;
    Result = sqlite3_prepare_v2(
      DB, "SELECT iteration FROM info LIMIT 1",
      -1, &Stmt, nullptr);
    assert(Result == SQLITE_OK);

    // This statement should always succeed.
    Result = sqlite3_step(Stmt);
    if (Result != SQLITE_ROW)
      abort();

    assert(sqlite3_column_count(Stmt) == 1);
    uint64_t Iteration = sqlite3_column_int64(Stmt, 0);

    sqlite3_finalize(Stmt);

    return Iteration;
  }

  virtual void setCurrentIteration(uint64_t Value) override {
    sqlite3_stmt* Stmt;
    int Result;
    Result = sqlite3_prepare_v2(
      DB, "UPDATE info SET iteration = ? WHERE id == 0;",
      -1, &Stmt, nullptr);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(Stmt, /*index=*/1, Value);
    assert(Result == SQLITE_OK);

    // This statement should always succeed.
    Result = sqlite3_step(Stmt);
    if (Result != SQLITE_DONE)
      abort();

    sqlite3_finalize(Stmt);
  }

  virtual bool lookupRuleResult(const Rule& Rule, Result* Result_Out) override {
    assert(Result_Out->BuiltAt == 0);

    // Fetch the basic rule information.
    sqlite3_stmt* Stmt;
    int Result;
    Result = sqlite3_prepare_v2(
      DB, ("SELECT id, value, built_at, computed_at FROM rule_results "
           "WHERE key == ? LIMIT 1;"),
      -1, &Stmt, nullptr);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_text(Stmt, /*index=*/1, Rule.Key.c_str(), -1,
                               SQLITE_STATIC);
    assert(Result == SQLITE_OK);

    // If the rule wasn't found, we are done.
    Result = sqlite3_step(Stmt);
    if (Result == SQLITE_DONE) {
      sqlite3_finalize(Stmt);
      return false;
    }
    if (Result != SQLITE_ROW)
      abort();
    
    // Otherwise, read the result contents from the row.
    assert(sqlite3_column_count(Stmt) == 4);
    uint64_t RuleID = sqlite3_column_int64(Stmt, 0);
    Result_Out->Value = sqlite3_column_int(Stmt, 1);
    Result_Out->BuiltAt = sqlite3_column_int64(Stmt, 2);
    Result_Out->ComputedAt = sqlite3_column_int64(Stmt, 3);
    sqlite3_finalize(Stmt);

    // Look up all the rule dependencies.
    Result = sqlite3_prepare_v2(DB, ("SELECT key FROM rule_dependencies "
                                     "WHERE rule_id == ?;"),
                                -1, &Stmt, nullptr);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(Stmt, /*index=*/1, RuleID);
    assert(Result == SQLITE_OK);
    
    while (true) {
      Result = sqlite3_step(Stmt);
      if (Result == SQLITE_DONE)
        break;
      assert(Result == SQLITE_ROW);
      if (Result != SQLITE_ROW) {
        abort();
      }
      assert(sqlite3_column_count(Stmt) == 1);
      Result_Out->Dependencies.push_back(
        (const char*)sqlite3_column_text(Stmt, 0));
    }
    sqlite3_finalize(Stmt);

    return true;
  }

  virtual void setRuleResult(const Rule& Rule,
                             const Result& RuleResult) override {
    sqlite3_stmt* Stmt;
    int Result;
 
    // Find the existing rule id, if present.
    //
    // We rely on SQLite3 not using 0 as a valid rule ID.
    Result = sqlite3_prepare_v2(
      DB, ("SELECT id FROM rule_results "
           "WHERE key == ? LIMIT 1;"),
      -1, &Stmt, nullptr);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_text(Stmt, /*index=*/1, Rule.Key.c_str(), -1,
                               SQLITE_STATIC);
    assert(Result == SQLITE_OK);

    uint64_t RuleID = 0;
    Result = sqlite3_step(Stmt);
    if (Result == SQLITE_DONE) {
      RuleID = 0;
    } else if (Result == SQLITE_ROW) {
      assert(sqlite3_column_count(Stmt) == 1);
      RuleID = sqlite3_column_int64(Stmt, 0);
    } else {
      abort();
    }
    sqlite3_finalize(Stmt);

    // If there is an existing entry, delete it first.
    //
    // FIXME: This is inefficient, we should just perform an update.
    if (RuleID != 0) {
      // Delete all of the rule dependencies.
      Result = sqlite3_prepare_v2(
        DB, "DELETE FROM rule_dependencies WHERE rule_id == ?",
        -1, &Stmt, nullptr);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_int64(Stmt, /*index=*/1, RuleID);
      assert(Result == SQLITE_OK);
      Result = sqlite3_step(Stmt);
      if (Result != SQLITE_DONE)
        abort();
      sqlite3_finalize(Stmt);

      // Delete the rule result.
      Result = sqlite3_prepare_v2(
        DB, "DELETE FROM rule_results WHERE id == ?",
        -1, &Stmt, nullptr);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_int64(Stmt, /*index=*/1, RuleID);
      assert(Result == SQLITE_OK);
      Result = sqlite3_step(Stmt);
      if (Result != SQLITE_DONE)
        abort();
      sqlite3_finalize(Stmt);
    }

    // Insert the actual rule result.
    Result = sqlite3_prepare_v2(
      DB, "INSERT INTO rule_results VALUES (NULL, ?, ?, ?, ?);",
      -1, &Stmt, nullptr);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_text(Stmt, /*index=*/1, Rule.Key.c_str(), -1,
                               SQLITE_STATIC);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(Stmt, /*index=*/2, RuleResult.Value);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(Stmt, /*index=*/3, RuleResult.BuiltAt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(Stmt, /*index=*/4, RuleResult.ComputedAt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_step(Stmt);
    if (Result != SQLITE_DONE)
      abort();
    sqlite3_finalize(Stmt);

    // Get the rule ID.
    RuleID = sqlite3_last_insert_rowid(DB);

    // Insert all the dependencies.
    for (auto& Dependency: RuleResult.Dependencies) {
      Result = sqlite3_prepare_v2(
        DB, "INSERT INTO rule_dependencies VALUES (NULL, ?, ?);",
        -1, &Stmt, nullptr);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_int64(Stmt, /*index=*/1, RuleID);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_text(Stmt, /*index=*/2, Dependency.c_str(), -1,
                                 SQLITE_STATIC);
      assert(Result == SQLITE_OK);
      Result = sqlite3_step(Stmt);
      if (Result != SQLITE_DONE)
        abort();
      sqlite3_finalize(Stmt);
    }
  }

  virtual void buildStarted() override {
    // Execute the entire build inside a single transaction.
    //
    // FIXME: We should revist this, as we probably wouldn't want a crash in the
    // build system to totally lose all build results.
    int Result = sqlite3_exec(DB, "BEGIN IMMEDIATE;", nullptr, nullptr,
        nullptr);
    assert(Result == SQLITE_OK);
    (void)Result;
  }

  virtual void buildComplete() override {
    // Sync changes to disk.
    int Result = sqlite3_exec(DB, "END;", nullptr, nullptr, nullptr);
    assert(Result == SQLITE_OK);
    (void)Result;
  }

  /// @}
};

}

std::unique_ptr<BuildDB> core::CreateSQLiteBuildDB(const std::string& Path,
                                                   std::string* Error_Out) {
  std::unique_ptr<SQLiteBuildDB> DB(new SQLiteBuildDB);
  if (!DB->open(Path, Error_Out))
    return nullptr;

  return std::move(DB);
}
