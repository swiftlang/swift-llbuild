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
  static const int CurrentSchemaVersion = 3;

  sqlite3 *DB = nullptr;

public:
  virtual ~SQLiteBuildDB() {
    if (DB)
      close();
  }

  bool open(const std::string& Path, uint32_t ClientSchemaVersion,
            std::string *Error_Out) {
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
    uint32_t ClientVersion = 0;
    sqlite3_stmt* Stmt;
    Result = sqlite3_prepare_v2(
      DB, "SELECT version,client_version FROM info LIMIT 1",
      -1, &Stmt, nullptr);
    if (Result == SQLITE_ERROR) {
      Version = -1;
    } else {
      assert(Result == SQLITE_OK);

      Result = sqlite3_step(Stmt);
      if (Result == SQLITE_DONE) {
        Version = -1;
      } else if (Result == SQLITE_ROW) {
        assert(sqlite3_column_count(Stmt) == 2);
        Version = sqlite3_column_int(Stmt, 0);
        ClientVersion = sqlite3_column_int(Stmt, 1);
      } else {
        abort();
      }
      sqlite3_finalize(Stmt);
    }

    if (Version != CurrentSchemaVersion ||
        ClientVersion != ClientSchemaVersion) {
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
             "client_version INTEGER, "
             "iteration INTEGER);"),
        nullptr, nullptr, &CError);
      if (Result == SQLITE_OK) {
        char* Query = sqlite3_mprintf(
          "INSERT INTO info VALUES (0, %d, %d, 0);",
          CurrentSchemaVersion, ClientSchemaVersion);
        Result = sqlite3_exec(DB, Query, nullptr, nullptr, &CError);
        free(Query);
      }
      if (Result == SQLITE_OK) {
        Result = sqlite3_exec(
          DB, ("CREATE TABLE rule_results ("
               "id INTEGER PRIMARY KEY, "
               "key STRING, "
               "value BLOB, "
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

    // Initialize prepared statements.
    Result = sqlite3_prepare_v2(
      DB, FindIDForKeyInRuleResultsStmtSQL,
      -1, &FindIDForKeyInRuleResultsStmt, nullptr);
    assert(Result == SQLITE_OK);

    Result = sqlite3_prepare_v2(
      DB, InsertIntoRuleResultsStmtSQL,
      -1, &InsertIntoRuleResultsStmt, nullptr);
    assert(Result == SQLITE_OK);

    Result = sqlite3_prepare_v2(
      DB, InsertIntoRuleDependenciesStmtSQL,
      -1, &InsertIntoRuleDependenciesStmt, nullptr);
    assert(Result == SQLITE_OK);

    Result = sqlite3_prepare_v2(
      DB, DeleteFromRuleResultsStmtSQL,
      -1, &DeleteFromRuleResultsStmt, nullptr);
    assert(Result == SQLITE_OK);

    Result = sqlite3_prepare_v2(
      DB, DeleteFromRuleDependenciesStmtSQL,
      -1, &DeleteFromRuleDependenciesStmt, nullptr);
    assert(Result == SQLITE_OK);

    Result = sqlite3_prepare_v2(
      DB, FindRuleResultStmtSQL,
      -1, &FindRuleResultStmt, nullptr);
    assert(Result == SQLITE_OK);

    Result = sqlite3_prepare_v2(
      DB, FindRuleDependenciesStmtSQL,
      -1, &FindRuleDependenciesStmt, nullptr);
    assert(Result == SQLITE_OK);

    return true;
  }

  void close() {
    assert(DB);

    // Destroy prepared statements.
    sqlite3_finalize(FindRuleDependenciesStmt);
    sqlite3_finalize(FindRuleResultStmt);
    sqlite3_finalize(DeleteFromRuleDependenciesStmt);
    sqlite3_finalize(DeleteFromRuleResultsStmt);
    sqlite3_finalize(InsertIntoRuleDependenciesStmt);
    sqlite3_finalize(InsertIntoRuleResultsStmt);
    sqlite3_finalize(FindIDForKeyInRuleResultsStmt);

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

  static constexpr const char *FindRuleResultStmtSQL =
    "SELECT id, value, built_at, computed_at FROM rule_results "
    "WHERE key == ? LIMIT 1;";
  sqlite3_stmt* FindRuleResultStmt = nullptr;

  static constexpr const char *FindRuleDependenciesStmtSQL =
    "SELECT key FROM rule_dependencies "
    "WHERE rule_id == ?;";
  sqlite3_stmt* FindRuleDependenciesStmt = nullptr;

  virtual bool lookupRuleResult(const Rule& Rule, Result* Result_Out) override {
    assert(Result_Out->BuiltAt == 0);

    // Fetch the basic rule information.
    int Result;
    Result = sqlite3_reset(FindRuleResultStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_clear_bindings(FindRuleResultStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_text(FindRuleResultStmt, /*index=*/1,
                               Rule.Key.c_str(), -1,
                               SQLITE_STATIC);
    assert(Result == SQLITE_OK);

    // If the rule wasn't found, we are done.
    Result = sqlite3_step(FindRuleResultStmt);
    if (Result == SQLITE_DONE)
      return false;
    if (Result != SQLITE_ROW)
      abort();

    // Otherwise, read the result contents from the row.
    assert(sqlite3_column_count(FindRuleResultStmt) == 4);
    uint64_t RuleID = sqlite3_column_int64(FindRuleResultStmt, 0);
    int NumValueBytes = sqlite3_column_bytes(FindRuleResultStmt, 1);
    Result_Out->Value.resize(NumValueBytes);
    memcpy(Result_Out->Value.data(),
           sqlite3_column_blob(FindRuleResultStmt, 1),
           NumValueBytes);
    Result_Out->BuiltAt = sqlite3_column_int64(FindRuleResultStmt, 2);
    Result_Out->ComputedAt = sqlite3_column_int64(FindRuleResultStmt, 3);

    // Look up all the rule dependencies.
    Result = sqlite3_reset(FindRuleDependenciesStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_clear_bindings(FindRuleDependenciesStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(FindRuleDependenciesStmt, /*index=*/1,
                                RuleID);
    assert(Result == SQLITE_OK);

    while (true) {
      Result = sqlite3_step(FindRuleDependenciesStmt);
      if (Result == SQLITE_DONE)
        break;
      assert(Result == SQLITE_ROW);
      if (Result != SQLITE_ROW) {
        abort();
      }
      assert(sqlite3_column_count(FindRuleDependenciesStmt) == 1);
      Result_Out->Dependencies.push_back(
        (const char*)sqlite3_column_text(FindRuleDependenciesStmt, 0));
    }

    return true;
  }

  static constexpr const char *FindIDForKeyInRuleResultsStmtSQL =
    "SELECT id FROM rule_results "
    "WHERE key == ? LIMIT 1;";
  sqlite3_stmt* FindIDForKeyInRuleResultsStmt = nullptr;

  static constexpr const char *InsertIntoRuleResultsStmtSQL =
    "INSERT INTO rule_results VALUES (NULL, ?, ?, ?, ?);";
  sqlite3_stmt* InsertIntoRuleResultsStmt = nullptr;

  static constexpr const char *InsertIntoRuleDependenciesStmtSQL =
    "INSERT INTO rule_dependencies VALUES (NULL, ?, ?);";
  sqlite3_stmt* InsertIntoRuleDependenciesStmt = nullptr;

  static constexpr const char *DeleteFromRuleResultsStmtSQL =
    "DELETE FROM rule_results WHERE id == ?;";
  sqlite3_stmt* DeleteFromRuleResultsStmt = nullptr;

  static constexpr const char *DeleteFromRuleDependenciesStmtSQL =
    "DELETE FROM rule_dependencies WHERE rule_id == ?;";
  sqlite3_stmt* DeleteFromRuleDependenciesStmt = nullptr;

  virtual void setRuleResult(const Rule& Rule,
                             const Result& RuleResult) override {
    int Result;

    // Find the existing rule id, if present.
    //
    // We rely on SQLite3 not using 0 as a valid rule ID.
    Result = sqlite3_reset(FindIDForKeyInRuleResultsStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_clear_bindings(FindIDForKeyInRuleResultsStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_text(FindIDForKeyInRuleResultsStmt,
                               /*index=*/1, Rule.Key.c_str(), -1,
                               SQLITE_STATIC);
    assert(Result == SQLITE_OK);

    uint64_t RuleID = 0;
    Result = sqlite3_step(FindIDForKeyInRuleResultsStmt);
    if (Result == SQLITE_DONE) {
      RuleID = 0;
    } else if (Result == SQLITE_ROW) {
      assert(sqlite3_column_count(FindIDForKeyInRuleResultsStmt) == 1);
      RuleID = sqlite3_column_int64(FindIDForKeyInRuleResultsStmt, 0);
    } else {
      abort();
    }

    // If there is an existing entry, delete it first.
    //
    // FIXME: This is inefficient, we should just perform an update.
    if (RuleID != 0) {
      // Delete all of the rule dependencies.
      Result = sqlite3_reset(DeleteFromRuleDependenciesStmt);
      assert(Result == SQLITE_OK);
      Result = sqlite3_clear_bindings(DeleteFromRuleDependenciesStmt);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_int64(DeleteFromRuleDependenciesStmt, /*index=*/1,
                                  RuleID);
      assert(Result == SQLITE_OK);
      Result = sqlite3_step(DeleteFromRuleDependenciesStmt);
      if (Result != SQLITE_DONE)
        abort();

      // Delete the rule result.
      Result = sqlite3_reset(DeleteFromRuleResultsStmt);
      assert(Result == SQLITE_OK);
      Result = sqlite3_clear_bindings(DeleteFromRuleResultsStmt);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_int64(DeleteFromRuleResultsStmt, /*index=*/1,
                                  RuleID);
      assert(Result == SQLITE_OK);
      Result = sqlite3_step(DeleteFromRuleResultsStmt);
      if (Result != SQLITE_DONE)
        abort();
    }

    // Insert the actual rule result.
    Result = sqlite3_reset(InsertIntoRuleResultsStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_clear_bindings(InsertIntoRuleResultsStmt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_text(InsertIntoRuleResultsStmt, /*index=*/1,
                               Rule.Key.c_str(), -1,
                               SQLITE_STATIC);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_blob(InsertIntoRuleResultsStmt, /*index=*/2,
                               RuleResult.Value.data(),
                               RuleResult.Value.size(),
                               SQLITE_STATIC);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(InsertIntoRuleResultsStmt, /*index=*/3,
                                RuleResult.BuiltAt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_bind_int64(InsertIntoRuleResultsStmt, /*index=*/4,
                                RuleResult.ComputedAt);
    assert(Result == SQLITE_OK);
    Result = sqlite3_step(InsertIntoRuleResultsStmt);
    if (Result != SQLITE_DONE)
      abort();

    // Get the rule ID.
    RuleID = sqlite3_last_insert_rowid(DB);

    // Insert all the dependencies.
    for (auto& Dependency: RuleResult.Dependencies) {
      Result = sqlite3_reset(InsertIntoRuleDependenciesStmt);
      assert(Result == SQLITE_OK);
      Result = sqlite3_clear_bindings(InsertIntoRuleDependenciesStmt);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_int64(InsertIntoRuleDependenciesStmt, /*index=*/1,
                                  RuleID);
      assert(Result == SQLITE_OK);
      Result = sqlite3_bind_text(InsertIntoRuleDependenciesStmt, /*index=*/2,
                                 Dependency.c_str(), -1,
                                 SQLITE_STATIC);
      assert(Result == SQLITE_OK);
      Result = sqlite3_step(InsertIntoRuleDependenciesStmt);
      if (Result != SQLITE_DONE)
        abort();
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
                                                   uint32_t ClientSchemaVersion,
                                                   std::string* Error_Out) {
  std::unique_ptr<SQLiteBuildDB> DB(new SQLiteBuildDB);
  if (!DB->open(Path, ClientSchemaVersion, Error_Out))
    return nullptr;

  return std::move(DB);
}
