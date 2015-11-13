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

#include "llvm/ADT/STLExtras.h"

#include <cassert>
#include <cerrno>
#include <cstring>

#include <sqlite3.h>
#include <unistd.h>

using namespace llbuild;
using namespace llbuild::core;

// SQLite BuildDB Implementation

// FIXME: This entire implementation needs to be updated for good error
// handling.

namespace {

class SQLiteBuildDB : public BuildDB {
  static const int currentSchemaVersion = 4;

  sqlite3 *db = nullptr;

public:
  virtual ~SQLiteBuildDB() {
    if (db)
      close();
  }

  bool open(StringRef path, uint32_t clientSchemaVersion,
            std::string *error_out) {
    assert(!db);
    int result = sqlite3_open(path.str().c_str(), &db);
    if (result != SQLITE_OK) {
      // FIXME: Provide better error messages.
      *error_out = "unable to open database";
      return false;
    }

    // Create the database schema, if necessary.
    char *cError;
    int version;
    uint32_t clientVersion = 0;
    sqlite3_stmt* stmt;
    result = sqlite3_prepare_v2(
      db, "SELECT version,client_version FROM info LIMIT 1",
      -1, &stmt, nullptr);
    if (result == SQLITE_ERROR) {
      version = -1;
    } else {
      assert(result == SQLITE_OK);

      result = sqlite3_step(stmt);
      if (result == SQLITE_DONE) {
        version = -1;
      } else if (result == SQLITE_ROW) {
        assert(sqlite3_column_count(stmt) == 2);
        version = sqlite3_column_int(stmt, 0);
        clientVersion = sqlite3_column_int(stmt, 1);
      } else {
        abort();
      }
      sqlite3_finalize(stmt);
    }

    if (version != currentSchemaVersion ||
        clientVersion != clientSchemaVersion) {
      // Close the database before we try to recreate it.
      sqlite3_close(db);

      // Always recreate the database from scratch when the schema changes.
      result = unlink(path.str().c_str());
      if (result == -1) {
        if (errno != ENOENT) {
          *error_out = std::string("unable to unlink existing database: ") +
            ::strerror(errno);
          sqlite3_close(db);
          return false;
        }
      } else {
        // If the remove was successful, reopen the database.
        int result = sqlite3_open(path.str().c_str(), &db);
        if (result != SQLITE_OK) {
          // FIXME: Provide better error messages.
          *error_out = "unable to open database";
          return false;
        }
      }

      // Create the schema in a single transaction.
      result = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &cError);

      // Create the info table.
      if (result == SQLITE_OK) {
	result = sqlite3_exec(
          db, ("CREATE TABLE info ("
	       "id INTEGER PRIMARY KEY, "
	       "version INTEGER, "
	       "client_version INTEGER, "
	       "iteration INTEGER);"),
	  nullptr, nullptr, &cError);
      }
      if (result == SQLITE_OK) {
        char* query = sqlite3_mprintf(
          "INSERT INTO info VALUES (0, %d, %d, 0);",
          currentSchemaVersion, clientSchemaVersion);
        result = sqlite3_exec(db, query, nullptr, nullptr, &cError);
        sqlite3_free(query);
      }
      if (result == SQLITE_OK) {
        result = sqlite3_exec(
          db, ("CREATE TABLE rule_results ("
               "id INTEGER PRIMARY KEY, "
               "key STRING, "
               "value BLOB, "
               "built_at INTEGER, "
               "computed_at INTEGER);"),
          nullptr, nullptr, &cError);
      }
      if (result == SQLITE_OK) {
        // Create the table used for storing rule dependencies.
        //
        // In order to reduce duplication, we use a WITHOUT ROWID table with a
        // composite key on the rule_id and the dependency key. This allows us
        // to use the table itself to perform efficient queries for all of the
        // keys associated with a particular rule_id, and by doing so avoid
        // having an ancillary index with duplicate data.
        result = sqlite3_exec(
          db, ("CREATE TABLE rule_dependencies ("
               "rule_id INTEGER, "
               "key STRING, "
               "PRIMARY KEY (rule_id, key) "
               "FOREIGN KEY(rule_id) REFERENCES rule_info(id)) "
               "WITHOUT ROWID;"),
          nullptr, nullptr, &cError);
      }

      // Create the indices on the rule tables.
      if (result == SQLITE_OK) {
        // Create an index to be used for efficiently looking up rule
        // information from a key.
        result = sqlite3_exec(
          db, "CREATE UNIQUE INDEX rule_results_idx ON rule_results (key);",
          nullptr, nullptr, &cError);
      }

      // Sync changes to disk.
      if (result == SQLITE_OK) {
	result = sqlite3_exec(db, "END;", nullptr, nullptr, &cError);
      }

      if (result != SQLITE_OK) {
        *error_out = (std::string("unable to initialize database (") + cError
                      + ")");
        sqlite3_free(cError);
        sqlite3_close(db);
        return false;
      }
    }

    // Initialize prepared statements.
    result = sqlite3_prepare_v2(
      db, findIDForKeyInRuleResultsStmtSQL,
      -1, &findIDForKeyInRuleResultsStmt, nullptr);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(
      db, insertIntoRuleResultsStmtSQL,
      -1, &insertIntoRuleResultsStmt, nullptr);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(
      db, insertIntoRuleDependenciesStmtSQL,
      -1, &insertIntoRuleDependenciesStmt, nullptr);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(
      db, deleteFromRuleResultsStmtSQL,
      -1, &deleteFromRuleResultsStmt, nullptr);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(
      db, deleteFromRuleDependenciesStmtSQL,
      -1, &deleteFromRuleDependenciesStmt, nullptr);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(
      db, findRuleResultStmtSQL,
      -1, &findRuleResultStmt, nullptr);
    assert(result == SQLITE_OK);

    result = sqlite3_prepare_v2(
      db, findRuleDependenciesStmtSQL,
      -1, &findRuleDependenciesStmt, nullptr);
    assert(result == SQLITE_OK);

    return true;
  }

  void close() {
    assert(db);

    // Destroy prepared statements.
    sqlite3_finalize(findRuleDependenciesStmt);
    sqlite3_finalize(findRuleResultStmt);
    sqlite3_finalize(deleteFromRuleDependenciesStmt);
    sqlite3_finalize(deleteFromRuleResultsStmt);
    sqlite3_finalize(insertIntoRuleDependenciesStmt);
    sqlite3_finalize(insertIntoRuleResultsStmt);
    sqlite3_finalize(findIDForKeyInRuleResultsStmt);

    sqlite3_close(db);
    db = nullptr;
  }

  /// @name BuildDB API
  /// @{

  virtual uint64_t getCurrentIteration() override {
    assert(db);

    // Fetch the iteration from the info table.
    sqlite3_stmt* stmt;
    int result;
    result = sqlite3_prepare_v2(
      db, "SELECT iteration FROM info LIMIT 1",
      -1, &stmt, nullptr);
    assert(result == SQLITE_OK);

    // This statement should always succeed.
    result = sqlite3_step(stmt);
    if (result != SQLITE_ROW)
      abort();

    assert(sqlite3_column_count(stmt) == 1);
    uint64_t iteration = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    return iteration;
  }

  virtual void setCurrentIteration(uint64_t value) override {
    sqlite3_stmt* stmt;
    int result;
    result = sqlite3_prepare_v2(
      db, "UPDATE info SET iteration = ? WHERE id == 0;",
      -1, &stmt, nullptr);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_int64(stmt, /*index=*/1, value);
    assert(result == SQLITE_OK);

    // This statement should always succeed.
    result = sqlite3_step(stmt);
    if (result != SQLITE_DONE)
      abort();

    sqlite3_finalize(stmt);
  }

  static constexpr const char *findRuleResultStmtSQL =
    "SELECT id, value, built_at, computed_at FROM rule_results "
    "WHERE key == ? LIMIT 1;";
  sqlite3_stmt* findRuleResultStmt = nullptr;

  static constexpr const char *findRuleDependenciesStmtSQL =
    "SELECT key FROM rule_dependencies "
    "WHERE rule_id == ?;";
  sqlite3_stmt* findRuleDependenciesStmt = nullptr;

  virtual bool lookupRuleResult(const Rule& rule, Result* result_out) override {
    assert(result_out->builtAt == 0);

    // Fetch the basic rule information.
    int result;
    result = sqlite3_reset(findRuleResultStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_clear_bindings(findRuleResultStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_text(findRuleResultStmt, /*index=*/1,
                               rule.key.data(), rule.key.size(),
                               SQLITE_STATIC);
    assert(result == SQLITE_OK);

    // If the rule wasn't found, we are done.
    result = sqlite3_step(findRuleResultStmt);
    if (result == SQLITE_DONE)
      return false;
    if (result != SQLITE_ROW)
      abort();

    // Otherwise, read the result contents from the row.
    assert(sqlite3_column_count(findRuleResultStmt) == 4);
    uint64_t ruleID = sqlite3_column_int64(findRuleResultStmt, 0);
    int numValueBytes = sqlite3_column_bytes(findRuleResultStmt, 1);
    result_out->value.resize(numValueBytes);
    memcpy(result_out->value.data(),
           sqlite3_column_blob(findRuleResultStmt, 1),
           numValueBytes);
    result_out->builtAt = sqlite3_column_int64(findRuleResultStmt, 2);
    result_out->computedAt = sqlite3_column_int64(findRuleResultStmt, 3);

    // Look up all the rule dependencies.
    result = sqlite3_reset(findRuleDependenciesStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_clear_bindings(findRuleDependenciesStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_int64(findRuleDependenciesStmt, /*index=*/1,
                                ruleID);
    assert(result == SQLITE_OK);

    while (true) {
      result = sqlite3_step(findRuleDependenciesStmt);
      if (result == SQLITE_DONE)
        break;
      assert(result == SQLITE_ROW);
      if (result != SQLITE_ROW) {
        abort();
      }
      assert(sqlite3_column_count(findRuleDependenciesStmt) == 1);
      result_out->dependencies.push_back(
          std::string(
              (const char*)sqlite3_column_text(findRuleDependenciesStmt, 0),
              sqlite3_column_bytes(findRuleDependenciesStmt, 0)));
    }

    return true;
  }

  static constexpr const char *findIDForKeyInRuleResultsStmtSQL =
    "SELECT id FROM rule_results "
    "WHERE key == ? LIMIT 1;";
  sqlite3_stmt* findIDForKeyInRuleResultsStmt = nullptr;

  static constexpr const char *insertIntoRuleResultsStmtSQL =
    "INSERT INTO rule_results VALUES (NULL, ?, ?, ?, ?);";
  sqlite3_stmt* insertIntoRuleResultsStmt = nullptr;

  /// The query for inserting new rule dependencies.
  ///
  /// Note that we explicitly allow ignoring the insert which will happen when
  /// there is a duplicate dependencies for a rule, because the primary key is a
  /// composite of (rule_id, key).
  static constexpr const char *insertIntoRuleDependenciesStmtSQL =
    "INSERT OR IGNORE INTO rule_dependencies(rule_id, key) VALUES (?, ?);";
  sqlite3_stmt* insertIntoRuleDependenciesStmt = nullptr;

  static constexpr const char *deleteFromRuleResultsStmtSQL =
    "DELETE FROM rule_results WHERE id == ?;";
  sqlite3_stmt* deleteFromRuleResultsStmt = nullptr;

  static constexpr const char *deleteFromRuleDependenciesStmtSQL =
    "DELETE FROM rule_dependencies WHERE rule_id == ?;";
  sqlite3_stmt* deleteFromRuleDependenciesStmt = nullptr;

  virtual void setRuleResult(const Rule& rule,
                             const Result& ruleResult) override {
    int result;

    // Find the existing rule id, if present.
    //
    // We rely on SQLite3 not using 0 as a valid rule ID.
    result = sqlite3_reset(findIDForKeyInRuleResultsStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_clear_bindings(findIDForKeyInRuleResultsStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_text(findIDForKeyInRuleResultsStmt, /*index=*/1,
                               rule.key.data(), rule.key.size(),
                               SQLITE_STATIC);
    assert(result == SQLITE_OK);

    uint64_t ruleID = 0;
    result = sqlite3_step(findIDForKeyInRuleResultsStmt);
    if (result == SQLITE_DONE) {
      ruleID = 0;
    } else if (result == SQLITE_ROW) {
      assert(sqlite3_column_count(findIDForKeyInRuleResultsStmt) == 1);
      ruleID = sqlite3_column_int64(findIDForKeyInRuleResultsStmt, 0);
    } else {
      abort();
    }

    // If there is an existing entry, delete it first.
    //
    // FIXME: This is inefficient, we should just perform an update.
    if (ruleID != 0) {
      // Delete all of the rule dependencies.
      result = sqlite3_reset(deleteFromRuleDependenciesStmt);
      assert(result == SQLITE_OK);
      result = sqlite3_clear_bindings(deleteFromRuleDependenciesStmt);
      assert(result == SQLITE_OK);
      result = sqlite3_bind_int64(deleteFromRuleDependenciesStmt, /*index=*/1,
                                  ruleID);
      assert(result == SQLITE_OK);
      result = sqlite3_step(deleteFromRuleDependenciesStmt);
      if (result != SQLITE_DONE)
        abort();

      // Delete the rule result.
      result = sqlite3_reset(deleteFromRuleResultsStmt);
      assert(result == SQLITE_OK);
      result = sqlite3_clear_bindings(deleteFromRuleResultsStmt);
      assert(result == SQLITE_OK);
      result = sqlite3_bind_int64(deleteFromRuleResultsStmt, /*index=*/1,
                                  ruleID);
      assert(result == SQLITE_OK);
      result = sqlite3_step(deleteFromRuleResultsStmt);
      if (result != SQLITE_DONE)
        abort();
    }

    // Insert the actual rule result.
    result = sqlite3_reset(insertIntoRuleResultsStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_clear_bindings(insertIntoRuleResultsStmt);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_text(insertIntoRuleResultsStmt, /*index=*/1,
                               rule.key.data(), rule.key.size(),
                               SQLITE_STATIC);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_blob(insertIntoRuleResultsStmt, /*index=*/2,
                               ruleResult.value.data(),
                               ruleResult.value.size(),
                               SQLITE_STATIC);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_int64(insertIntoRuleResultsStmt, /*index=*/3,
                                ruleResult.builtAt);
    assert(result == SQLITE_OK);
    result = sqlite3_bind_int64(insertIntoRuleResultsStmt, /*index=*/4,
                                ruleResult.computedAt);
    assert(result == SQLITE_OK);
    result = sqlite3_step(insertIntoRuleResultsStmt);
    if (result != SQLITE_DONE)
      abort();

    // Get the rule ID.
    ruleID = sqlite3_last_insert_rowid(db);

    // Insert all the dependencies.
    for (auto& dependency: ruleResult.dependencies) {
      result = sqlite3_reset(insertIntoRuleDependenciesStmt);
      assert(result == SQLITE_OK);
      result = sqlite3_clear_bindings(insertIntoRuleDependenciesStmt);
      assert(result == SQLITE_OK);
      result = sqlite3_bind_int64(insertIntoRuleDependenciesStmt, /*index=*/1,
                                  ruleID);
      assert(result == SQLITE_OK);
      result = sqlite3_bind_text(insertIntoRuleDependenciesStmt, /*index=*/2,
                                 dependency.data(), dependency.size(),
                                 SQLITE_STATIC);
      assert(result == SQLITE_OK);
      result = sqlite3_step(insertIntoRuleDependenciesStmt);
      if (result != SQLITE_DONE)
        abort();
    }
  }

  virtual void buildStarted() override {
    // Execute the entire build inside a single transaction.
    //
    // FIXME: We should revist this, as we probably wouldn't want a crash in the
    // build system to totally lose all build results.
    int result = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr,
        nullptr);
    assert(result == SQLITE_OK);
    (void)result;
  }

  virtual void buildComplete() override {
    // Sync changes to disk.
    int result = sqlite3_exec(db, "END;", nullptr, nullptr, nullptr);
    assert(result == SQLITE_OK);
    (void)result;
  }

  /// @}
};

}

std::unique_ptr<BuildDB> core::createSQLiteBuildDB(StringRef path,
                                                   uint32_t clientSchemaVersion,
                                                   std::string* error_out) {
  auto db = llvm::make_unique<SQLiteBuildDB>();
  if (!db->open(path, clientSchemaVersion, error_out))
    return nullptr;

  return std::move(db);
}
