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

namespace {

static int SQLiteGetSingleUInt64(void *Context, int ArgC, char **ArgV,
                                 char **Column) {
  uint64_t *Result_Out = (uint64_t*)Context;
  assert(ArgC == 1);
  char *End;
  *Result_Out = strtoll(ArgV[0], &End, 10);
  assert(*End == '\0');
  return SQLITE_OK;
}

struct RuleLookupResult {
  uint64_t RuleID;
  Result *Result;
};
static int SQLiteGetRuleResult(void *Context, int ArgC, char **ArgV,
                               char **Column) {
  RuleLookupResult *Info = (RuleLookupResult*)Context;
  assert(ArgC == 4);
  char *End;
  Info->RuleID = strtoll(ArgV[0], &End, 10);
  assert(*End == '\0');
  Info->Result->Value = strtoll(ArgV[1], &End, 10);
  assert(*End == '\0');
  Info->Result->BuiltAt = strtoll(ArgV[2], &End, 10);
  assert(*End == '\0');
  Info->Result->ComputedAt = strtoll(ArgV[3], &End, 10);
  assert(*End == '\0');
  return SQLITE_OK;
}
static int SQLiteAppendRuleResultDependency(void *Context, int ArgC,
                                            char **ArgV, char **Column) {
  RuleLookupResult *Info = (RuleLookupResult*)Context;
  assert(ArgC == 1);
  Info->Result->Dependencies.push_back(std::string(ArgV[0]));
  return SQLITE_OK;
}

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
    uint64_t Version;
    Result = sqlite3_exec(DB, "SELECT version FROM info LIMIT 1",
                          SQLiteGetSingleUInt64, &Version, &CError);
    if (Result != SQLITE_OK || int(Version) != CurrentSchemaVersion) {
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
    char *CError;
    uint64_t Iteration;
    int Result = sqlite3_exec(DB, "SELECT iteration FROM info LIMIT 1",
                              SQLiteGetSingleUInt64, &Iteration, &CError);
    assert(Result == SQLITE_OK);

    return Iteration;
  }

  virtual void setCurrentIteration(uint64_t Value) override {
    char *CError;
    char* Query = sqlite3_mprintf(
      "UPDATE info SET iteration = %lld WHERE id == 0;",
      Value);
    int Result = sqlite3_exec(DB, Query, nullptr, nullptr, &CError);
    assert(Result == SQLITE_OK);
    free(Query);
  }

  virtual bool lookupRuleResult(const Rule& Rule, Result* Result_Out) override {
    // Fetch the basic rule information.
    assert(Result_Out->BuiltAt == 0);
    char *CError;
    char* Query = sqlite3_mprintf(
      ("SELECT id, value, built_at, computed_at FROM rule_results "
       "WHERE key == '%q' LIMIT 1;"),
      Rule.Key.c_str());
    RuleLookupResult RuleInfo = { 0, Result_Out };
    int Result = sqlite3_exec(DB, Query, SQLiteGetRuleResult, &RuleInfo,
                              &CError);
    assert(Result == SQLITE_OK);
    free(Query);

    // If the rule wasn't found, we are done.
    if (RuleInfo.RuleID == 0)
      return false;

    // Otherwise, look up all the rule dependencies.
    Query = sqlite3_mprintf(("SELECT key FROM rule_dependencies "
                             "WHERE rule_id == %lld;"),
                            RuleInfo.RuleID);
    Result = sqlite3_exec(DB, Query, SQLiteAppendRuleResultDependency,
                          &RuleInfo, &CError);
    assert(Result == SQLITE_OK);
    free(Query);

    return true;
  }

  virtual void setRuleResult(const Rule& Rule,
                             const Result& RuleResult) override {
    int Result = SQLITE_OK;
    assert(Result == SQLITE_OK);
 
    // Find the existing rule id, if present.
    //
    // We rely on SQLite3 not using 0 as a valid rule ID.
    char *CError;
    char* RuleIDQuery = sqlite3_mprintf(
      ("SELECT id FROM rule_results "
       "WHERE key == '%q' LIMIT 1;"),
      Rule.Key.c_str());
    uint64_t RuleID = 0;
    Result = sqlite3_exec(DB, RuleIDQuery,
                              SQLiteGetSingleUInt64, &RuleID, &CError);
    assert(Result == SQLITE_OK);

    // If there is an existing entry, delete it first.
    //
    // FIXME: This is inefficient, we should just perform an update.
    if (RuleID != 0) {
      char* Query = sqlite3_mprintf(
        "DELETE FROM rule_dependencies WHERE rule_id == %lld",
        RuleID);
      Result = sqlite3_exec(DB, Query, nullptr, nullptr, &CError);
      assert(Result == SQLITE_OK);
      free(Query);

      Query = sqlite3_mprintf(
        "DELETE FROM rule_results WHERE id == %lld",
        RuleID);
      Result = sqlite3_exec(DB, Query, nullptr, nullptr, &CError);
      assert(Result == SQLITE_OK);
      free(Query);
    }

    char* Query = sqlite3_mprintf(
      ("INSERT INTO rule_results VALUES (NULL, '%q', %d, %d, %d);"),
      Rule.Key.c_str(), RuleResult.Value, RuleResult.BuiltAt,
      RuleResult.ComputedAt);
    Result = sqlite3_exec(DB, Query, nullptr, nullptr, &CError);
    assert(Result == SQLITE_OK);
    free(Query);

    // Get the rule ID.
    RuleID = sqlite3_last_insert_rowid(DB);

    // Insert all the dependencies.
    for (auto& Dependency: RuleResult.Dependencies) {
      char* Query = sqlite3_mprintf(
        ("INSERT INTO rule_dependencies VALUES (NULL, %lld, '%q');"),
        RuleID, Dependency.c_str());
      Result = sqlite3_exec(DB, Query, nullptr, nullptr, &CError);
      assert(Result == SQLITE_OK);
      free(Query);
    }

    assert(Result == SQLITE_OK);
  }

  virtual void buildStarted() override {
    // Execute the entire build inside a single transaction.
    //
    // FIXME: We should revist this, as we probably wouldn't want a crash in the
    // build system to totally lose all build results.
    int Result = sqlite3_exec(DB, "BEGIN IMMEDIATE;", nullptr, nullptr,
        nullptr);
    assert(Result == SQLITE_OK);
  }

  virtual void buildComplete() override {
    int Result = sqlite3_exec(DB, "END;", nullptr, nullptr, nullptr);
    assert(Result == SQLITE_OK);

    // Sync changes to disk.
    close();
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
