//===-- SQLiteBuildDB.cpp -------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Core/BuildDB.h"

#include "llbuild/Basic/BinaryCoding.h"
#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Core/BuildEngine.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <mutex>

#include <sqlite3.h>

using namespace llbuild;
using namespace llbuild::core;

// SQLite BuildDB Implementation

/// Internal key ID type used for linking rule dependencies. Related to, but
/// distinct from the build engine's KeyID type.
struct DBKeyID {
  uint64_t value = 0;

  DBKeyID() { ; }
  DBKeyID(const DBKeyID&) = default;
  explicit DBKeyID(uint64_t value) : value(value) { ; }
};

// Provide DenseMapInfo for DBKeyID.
template<> struct ::llvm::DenseMapInfo<DBKeyID> {
  static inline DBKeyID getEmptyKey() { return DBKeyID(~0ULL); }
  static inline DBKeyID getTombstoneKey() { return DBKeyID(~0ULL - 1ULL); }
  static unsigned getHashValue(const DBKeyID& Val) {
    return (unsigned)(Val.value * 37ULL);
  }
  static bool isEqual(const DBKeyID& LHS, const DBKeyID& RHS) {
    return LHS.value == RHS.value;
  }
};

// Helper macro checking and returning error messages for failed SQLite calls
#define checkSQLiteResultOKReturnFalse(result) \
if (result != SQLITE_OK) { \
  *error_out = getCurrentErrorMessage(); \
  return false; \
}


namespace {

class SQLiteBuildDB : public BuildDB {
  /// Version History:
  /// * 17: Revert 15
  /// * 16: Add checksum field to FileInfo.
  /// * 15: Add barriers in dependency list.
  /// * 14: Filtered directory tree structure nodes, related build key changes
  /// * 13: Tagging dependencies with single-use flag.
  /// * 12: Tagging dependencies with order-only flag.
  /// * 11: Add result timestamps
  /// * 10: Add result signature
  /// * 9: Add filtered directory contents, related build key changes
  /// * 8: Remove ID from rule results
  /// * 7: De-normalized the rule result dependencies.
  /// * 6: Added `ordinal` field for dependencies.
  /// * 5: Switched to using `WITHOUT ROWID` for dependencies.
  /// * 4: Pre-history
  static const int currentSchemaVersion = 17;

  std::string path;
  uint32_t clientSchemaVersion;
  /// If this is `true`, the database will be re-created if the client/schema version mismatches.
  /// If `false`, it will not be re-created but returns an error instead.
  bool recreateOnUnmatchedVersion;

  sqlite3 *db = nullptr;

  /// The mutex to protect all access to the database and statements.
  std::mutex dbMutex;

  /// The delegate pointer
  BuildDBDelegate* delegate = nullptr;

  std::string getCurrentErrorMessage() {
    int err_code = sqlite3_errcode(db);
    const char* err_message = sqlite3_errmsg(db);
    const char* filename = sqlite3_db_filename(db, "main");

    std::string out;
    llvm::raw_string_ostream outStream(out);
    outStream << "error: accessing build database \"" << filename << "\": " << err_message;

    if (err_code == SQLITE_BUSY || err_code == SQLITE_LOCKED) {
      outStream << " Possibly there are two concurrent builds running in the same filesystem location.";
    }

    outStream.flush();
    return out;
  }

  bool open(std::string *error_out) {
    // The db is opened lazily whenever an operation on it occurs. Thus if it is
    // already open, we don't need to do any further work.
    if (db) return true;

    // Configure SQLite3 on first use.
    //
    // We attempt to set multi-threading mode, but can settle for serialized if
    // the library can't be reinitialized (there are only two modes).
    static int sqliteConfigureResult = []() -> int {
      // We access a single connection from multiple threads.
      return sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
    }();
    if (sqliteConfigureResult != SQLITE_OK) {
        if (!sqlite3_threadsafe()) {
            *error_out = "unable to configure database: not thread-safe";
            return false;
        }
    }

    int result = sqlite3_open(path.c_str(), &db);
    if (result != SQLITE_OK) {
      *error_out = "unable to open database: " + std::string(
          sqlite3_errstr(result));
      return false;
    }

    sqlite3_busy_timeout(db, 5000);
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
      if (result != SQLITE_OK) {
        *error_out = getCurrentErrorMessage();
        return false;
      }
      result = sqlite3_step(stmt);
      if (result == SQLITE_DONE) {
        version = -1;
      } else if (result == SQLITE_ROW) {
        assert(sqlite3_column_count(stmt) == 2);
        version = sqlite3_column_int(stmt, 0);
        clientVersion = sqlite3_column_int(stmt, 1);
      } else {
        *error_out = getCurrentErrorMessage();
        sqlite3_finalize(stmt);
        return false;
      }
      sqlite3_finalize(stmt);
    }

    if (version != currentSchemaVersion ||
        clientVersion != clientSchemaVersion) {
      // Close the database before we try to recreate it.
      sqlite3_close(db);
      db = nullptr;
      
      if (!recreateOnUnmatchedVersion) {
        // We don't re-create the database in this case and return an error
        *error_out = std::string("Version mismatch. (database-schema: ") + std::to_string(version) + std::string(" requested schema: ") + std::to_string(currentSchemaVersion) + std::string(". database-client: ") + std::to_string(clientVersion) + std::string(" requested client: ") + std::to_string(clientSchemaVersion) + std::string(")");
        return false;
      }

      // Always recreate the database from scratch when the schema changes.
      result = basic::sys::unlink(path.c_str());
      if (result == -1) {
        if (errno != ENOENT) {
          *error_out = std::string("unable to unlink existing database: ") +
            ::strerror(errno);
          sqlite3_close(db);
          db = nullptr;
          return false;
        }
      } else {
        // If the remove was successful, reopen the database.
        int result = sqlite3_open(path.c_str(), &db);
        if (result != SQLITE_OK) {
          *error_out = getCurrentErrorMessage();
          return false;
        }
      }

      // Create the schema in a single transaction.
      result = sqlite3_exec(db, "BEGIN EXCLUSIVE;", nullptr, nullptr, &cError);

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
          db, ("CREATE TABLE key_names ("
               "id INTEGER PRIMARY KEY, "
               "key STRING UNIQUE);"),
          nullptr, nullptr, &cError);
      }
      if (result == SQLITE_OK) {
        result = sqlite3_exec(
          db, ("CREATE TABLE rule_results ("
               "key_id INTEGER PRIMARY KEY, "
               "value BLOB, "
               "signature INTEGER, "
               "built_at INTEGER, "
               "computed_at INTEGER, "
               "start REAL, "
               "end REAL, "
               "dependencies BLOB, "
               "FOREIGN KEY(key_id) REFERENCES key_names(id));"),
          nullptr, nullptr, &cError);
      }

      // Create the indices on the rule tables.
      if (result == SQLITE_OK) {
        // Create an index to be used for efficiently looking up rule
        // information from a key.
        result = sqlite3_exec(
            db, "CREATE UNIQUE INDEX rule_results_idx ON rule_results (key_id);",
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
        db = nullptr;
        return false;
      }
    }

    // Initialize prepared statements.
    result = sqlite3_prepare_v2(
      db, findKeyIDForKeyStmtSQL,
      -1, &findKeyIDForKeyStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    result = sqlite3_prepare_v2(
      db, findKeyNameForKeyIDStmtSQL,
      -1, &findKeyNameForKeyIDStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    result = sqlite3_prepare_v2(
      db, insertIntoKeysStmtSQL,
      -1, &insertIntoKeysStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    result = sqlite3_prepare_v2(
      db, insertIntoRuleResultsStmtSQL,
      -1, &insertIntoRuleResultsStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    result = sqlite3_prepare_v2(
      db, deleteFromKeysStmtSQL,
      -1, &deleteFromKeysStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    result = sqlite3_prepare_v2(
      db, findRuleResultStmtSQL,
      -1, &findRuleResultStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    result = sqlite3_prepare_v2(
      db, fastFindRuleResultStmtSQL,
      -1, &fastFindRuleResultStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);
    
    result = sqlite3_prepare_v2(
      db, getKeysWithResultStmtSQL,
      -1, &getKeysWithResultStmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    return true;
  }

  void close() {
    if (!db) return;

    // Destroy prepared statements.
    sqlite3_finalize(findKeyIDForKeyStmt);
    findKeyIDForKeyStmt = nullptr;
    sqlite3_finalize(findKeyNameForKeyIDStmt);
    findKeyNameForKeyIDStmt = nullptr;
    sqlite3_finalize(findRuleResultStmt);
    findRuleResultStmt = nullptr;
    sqlite3_finalize(fastFindRuleResultStmt);
    fastFindRuleResultStmt = nullptr;
    sqlite3_finalize(deleteFromKeysStmt);
    deleteFromKeysStmt = nullptr;
    sqlite3_finalize(insertIntoKeysStmt);
    insertIntoKeysStmt = nullptr;
    sqlite3_finalize(insertIntoRuleResultsStmt);
    insertIntoRuleResultsStmt = nullptr;
    sqlite3_finalize(getKeysWithResultStmt);
    getKeysWithResultStmt = nullptr;

    int result = sqlite3_close(db);
    (void)result; // use the variable if we're building without asserts
    assert(result == SQLITE_OK && "The database connection could not be closed. That means there are prepared statements that are not finalized, data blobs that are not closed or backups not finished.");
    db = nullptr;
  }

public:
  SQLiteBuildDB(StringRef path, uint32_t clientSchemaVersion, bool recreateOnUnmatchedVersion)
    : path(path), clientSchemaVersion(clientSchemaVersion), recreateOnUnmatchedVersion(recreateOnUnmatchedVersion) { }

  virtual ~SQLiteBuildDB() {
    std::lock_guard<std::mutex> guard(dbMutex);
    if (db)
      close();
  }

  /// @name BuildDB API
  /// @{

  virtual void attachDelegate(BuildDBDelegate* delegate) override {
    this->delegate = delegate;
  }

  virtual Epoch getCurrentEpoch(bool* success_out, std::string *error_out) override {
    std::lock_guard<std::mutex> guard(dbMutex);

    if (!open(error_out)) {
      *success_out = false;
      return 0;
    }

    // Fetch the iteration from the info table.
    sqlite3_stmt* stmt;
    int result;
    result = sqlite3_prepare_v2(
      db, "SELECT iteration FROM info LIMIT 1",
      -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
      *success_out = false;
      *error_out = getCurrentErrorMessage();
      return 0;
    }

    result = sqlite3_step(stmt);
    if (result != SQLITE_ROW) {
      *success_out = false;
      *error_out = getCurrentErrorMessage();
      sqlite3_finalize(stmt);
      return 0;
    }

    assert(sqlite3_column_count(stmt) == 1);
    uint64_t iteration = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    *success_out = true;
    return iteration;
  }

  virtual bool setCurrentIteration(uint64_t value, std::string *error_out) override {
    std::lock_guard<std::mutex> guard(dbMutex);

    if (!open(error_out)) {
      return false;
    }

    sqlite3_stmt* stmt;
    int result;
    result = sqlite3_prepare_v2(
      db, "UPDATE info SET iteration = ? WHERE id == 0;",
      -1, &stmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_int64(stmt, /*index=*/1, value);
    checkSQLiteResultOKReturnFalse(result);

    result = sqlite3_step(stmt);
    if (result != SQLITE_DONE) {
      *error_out = getCurrentErrorMessage();
      sqlite3_finalize(stmt);
      return false;
    }

    sqlite3_finalize(stmt);
    return true;
  }

  static constexpr const char *deleteFromKeysStmtSQL = (
      "DELETE FROM key_names WHERE key == ?;");
  sqlite3_stmt* deleteFromKeysStmt = nullptr;

  // Although we have the engine's KeyID, we explictly use the key itself to
  // do the mapping via a table join. This is substantially more performant when
  // running an initial full build against empty tables. It is also essentially
  // equivalent to the mapping we would have to do for the DBKeyID, but defers
  // the creation of new IDs until we actually need them in setRuleResult().
  static constexpr const char *findRuleResultStmtSQL = (
      "SELECT rule_results.key_id, value, built_at, computed_at, start, end, dependencies, signature FROM rule_results "
      "INNER JOIN key_names ON key_names.id = rule_results.key_id WHERE key == ?;");
  sqlite3_stmt* findRuleResultStmt = nullptr;

  // Fast path find result for rules we already know they ID for
  static constexpr const char *fastFindRuleResultStmtSQL = (
      "SELECT key_id, value, built_at, computed_at, start, end, dependencies, signature FROM rule_results "
      "WHERE key_id == ?;");
  sqlite3_stmt* fastFindRuleResultStmt = nullptr;
  
  static constexpr const char *getKeysWithResultStmtSQL = (
      "SELECT rule_results.key_id, key_names.key, rule_results.value, rule_results.built_at, rule_results.computed_at, rule_results.start, rule_results.end, rule_results.dependencies, rule_results.signature FROM rule_results "
      "JOIN key_names WHERE rule_results.key_id == key_names.id;");
  sqlite3_stmt* getKeysWithResultStmt = nullptr;

  virtual bool lookupRuleResult(KeyID keyID, const KeyType& key,
                                Result* result_out,
                                std::string *error_out) override {
    assert(delegate != nullptr);
    std::lock_guard<std::mutex> guard(dbMutex);
    assert(result_out->builtAt == 0);

    if (!open(error_out)) {
      return false;
    }

    // Fetch the basic rule information.
    int result;
    int numDependencyBytes = 0;
    const void* dependencyBytes = nullptr;
    DBKeyID dbKeyID;

    // Check if we already have the key mapping
    auto it = dbKeyIDs.find(keyID);
    if (it != dbKeyIDs.end()) {
      // DBKeyID is known, perform the fast path that avoids table joining

      result = sqlite3_reset(fastFindRuleResultStmt);
      checkSQLiteResultOKReturnFalse(result);
      result = sqlite3_clear_bindings(fastFindRuleResultStmt);
      checkSQLiteResultOKReturnFalse(result);
      result = sqlite3_bind_int64(fastFindRuleResultStmt, /*index=*/1,
                                  it->second.value);
      checkSQLiteResultOKReturnFalse(result);

      // If the rule wasn't found, we are done.
      result = sqlite3_step(fastFindRuleResultStmt);
      if (result == SQLITE_DONE)
        return false;
      if (result != SQLITE_ROW) {
        *error_out = getCurrentErrorMessage();
        return false;
      }

      // Otherwise, read the result contents from the row.
      assert(sqlite3_column_count(fastFindRuleResultStmt) == 8);
      dbKeyID = DBKeyID(sqlite3_column_int64(fastFindRuleResultStmt, 0));
      int numValueBytes = sqlite3_column_bytes(fastFindRuleResultStmt, 1);
      result_out->value.resize(numValueBytes);
      memcpy(result_out->value.data(),
             sqlite3_column_blob(fastFindRuleResultStmt, 1),
             numValueBytes);
      result_out->builtAt = sqlite3_column_int64(fastFindRuleResultStmt, 2);
      result_out->computedAt = sqlite3_column_int64(fastFindRuleResultStmt, 3);
      result_out->start = sqlite3_column_double(fastFindRuleResultStmt, 4);
      result_out->end = sqlite3_column_double(fastFindRuleResultStmt, 5);

      // Extract the dependencies binary blob.
      numDependencyBytes = sqlite3_column_bytes(fastFindRuleResultStmt, 6);
      dependencyBytes = sqlite3_column_blob(fastFindRuleResultStmt, 6);

      // Extract the signature
      result_out->signature =
        basic::CommandSignature(sqlite3_column_int64(fastFindRuleResultStmt, 7));
    } else {
      // KeyID is not known, perform the 'normal' search using the key value

      result = sqlite3_reset(findRuleResultStmt);
      checkSQLiteResultOKReturnFalse(result);
      result = sqlite3_clear_bindings(findRuleResultStmt);
      checkSQLiteResultOKReturnFalse(result);
      result = sqlite3_bind_text(findRuleResultStmt, /*index=*/1,
                                 key.data(), key.size(),
                                 SQLITE_STATIC);
      checkSQLiteResultOKReturnFalse(result);

      // If the rule wasn't found, we are done.
      result = sqlite3_step(findRuleResultStmt);
      if (result == SQLITE_DONE)
        return false;
      if (result != SQLITE_ROW) {
        *error_out = getCurrentErrorMessage();
        return false;
      }

      // Otherwise, read the result contents from the row.
      assert(sqlite3_column_count(findRuleResultStmt) == 8);
      dbKeyID = DBKeyID(sqlite3_column_int64(findRuleResultStmt, 0));
      int numValueBytes = sqlite3_column_bytes(findRuleResultStmt, 1);
      result_out->value.resize(numValueBytes);
      memcpy(result_out->value.data(),
             sqlite3_column_blob(findRuleResultStmt, 1),
             numValueBytes);
      result_out->builtAt = sqlite3_column_int64(findRuleResultStmt, 2);
      result_out->computedAt = sqlite3_column_int64(findRuleResultStmt, 3);
      result_out->start = sqlite3_column_double(findRuleResultStmt, 4);
      result_out->end = sqlite3_column_double(findRuleResultStmt, 5);

      // Cache the engine key mapping
      engineKeyIDs[dbKeyID] = keyID;
      dbKeyIDs[keyID] = dbKeyID;

      // Extract the dependencies binary blob.
      numDependencyBytes = sqlite3_column_bytes(findRuleResultStmt, 6);
      dependencyBytes = sqlite3_column_blob(findRuleResultStmt, 6);

      // Extract the signature
      result_out->signature =
        basic::CommandSignature(sqlite3_column_int64(findRuleResultStmt, 7));
    }


    int numDependencies = numDependencyBytes / sizeof(uint64_t);
    if (numDependencyBytes != static_cast<int>(numDependencies * sizeof(uint64_t))) {
      *error_out = (llvm::Twine("unexpected contents for database result: ") +
                    llvm::Twine((int)dbKeyID.value)).str();
      return false;
    }
    result_out->dependencies.resize(numDependencies);
    basic::BinaryDecoder decoder(
        StringRef((const char*)dependencyBytes, numDependencyBytes));
    for (auto i = 0; i != numDependencies; ++i) {
      uint64_t raw;
      decoder.read(raw);
      bool orderOnly = raw & 1;
      bool singleUse = (raw >> 1) & 1;
      DBKeyID dbKeyID(raw >> 2);

      // Map the database key ID into an engine key ID (note that we already
      // hold the dbMutex at this point as required by getKeyIDforID())
      KeyID keyID = getKeyIDForID(dbKeyID, error_out);
      if (!error_out->empty()) {
        return false;
      }
      result_out->dependencies.set(i, keyID, orderOnly, singleUse);
    }

    return true;
  }

  static constexpr const char *insertIntoRuleResultsStmtSQL =
    "INSERT OR REPLACE INTO rule_results VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* insertIntoRuleResultsStmt = nullptr;

  static constexpr const char *findKeyIDForKeyStmtSQL = (
      "SELECT id FROM key_names "
      "WHERE key == ? LIMIT 1;");
  sqlite3_stmt* findKeyIDForKeyStmt = nullptr;

  static constexpr const char *findKeyNameForKeyIDStmtSQL = (
      "SELECT key FROM key_names "
      "WHERE id == ? LIMIT 1;");
  sqlite3_stmt* findKeyNameForKeyIDStmt = nullptr;

  static constexpr const char *insertIntoKeysStmtSQL =
  "INSERT OR IGNORE INTO key_names(key) VALUES (?);";
  sqlite3_stmt* insertIntoKeysStmt = nullptr;

  virtual bool setRuleResult(KeyID keyID,
                             const Rule& rule,
                             const Result& ruleResult,
                             std::string *error_out) override {
    assert(delegate != nullptr);
    std::lock_guard<std::mutex> guard(dbMutex);
    int result;

    if (!open(error_out)) {
      return false;
    }

    auto dbKeyID = getKeyID(keyID, error_out);
    if (!error_out->empty()) {
      return false;
    }

    // Create the encoded dependency list.
    //
    // FIXME: We could save some reallocation by having a templated SmallVector
    // size here.
    basic::BinaryEncoder encoder{};
    for (auto dependency: ruleResult.dependencies) {
      // Map the enging keyID to a database key ID
      //
      // FIXME: This is naively mapping all keys with no caching at this point,
      // thus likely to perform poorly.  Should refactor this into a bulk
      // query or a DB layer cache.
      auto dbKeyID = getKeyID(dependency.keyID, error_out);
      if (!error_out->empty()) {
        return false;
      }
      encoder.write((dbKeyID.value << 2) + (dependency.singleUse << 1) + dependency.orderOnly);
    }

    // Insert the actual rule result.
    result = sqlite3_reset(insertIntoRuleResultsStmt);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_clear_bindings(insertIntoRuleResultsStmt);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_int64(insertIntoRuleResultsStmt, /*index=*/1,
                               dbKeyID.value);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_blob(insertIntoRuleResultsStmt, /*index=*/2,
                               ruleResult.value.data(),
                               ruleResult.value.size(),
                               SQLITE_STATIC);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_int64(insertIntoRuleResultsStmt, /*index=*/3,
                               ruleResult.signature.value);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_int64(insertIntoRuleResultsStmt, /*index=*/4,
                                ruleResult.builtAt);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_int64(insertIntoRuleResultsStmt, /*index=*/5,
                                ruleResult.computedAt);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_double(insertIntoRuleResultsStmt, /*index=*/6,
                                ruleResult.start);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_double(insertIntoRuleResultsStmt, /*index=*/7,
                                ruleResult.end);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_bind_blob(insertIntoRuleResultsStmt, /*index=*/8,
                               encoder.data(),
                               encoder.size(),
                               SQLITE_STATIC);
    checkSQLiteResultOKReturnFalse(result);
    result = sqlite3_step(insertIntoRuleResultsStmt);
    if (result != SQLITE_DONE) {
      *error_out = getCurrentErrorMessage();
      return false;
    }

    return true;
  }

  virtual bool buildStarted(std::string *error_out) override {
    std::lock_guard<std::mutex> guard(dbMutex);

    if (!open(error_out))
      return false;

    // Execute the entire build inside a single transaction.
    //
    // FIXME: We should revist this, as we probably wouldn't want a crash in the
    // build system to totally lose all build results.
    int result = sqlite3_exec(db, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr);

    if (result != SQLITE_OK) {
      *error_out = getCurrentErrorMessage();
      return false;
    }

    return true;
  }

  virtual void buildComplete() override {
    std::lock_guard<std::mutex> guard(dbMutex);

    // Sync changes to disk.
    int result = sqlite3_exec(db, "END;", nullptr, nullptr, nullptr);
    assert(result == SQLITE_OK);
    (void)result;

    // We close the connection whenever a build completes so that we release
    // any locks that we may have on the file.
    close();
  }

  virtual bool getKeys(std::vector<KeyType>& keys_out, std::string* error_out) override {
    std::lock_guard<std::mutex> guard(dbMutex);

    if (!open(error_out))
      return false;

    // Search for the key in the database
    int result;
    sqlite3_stmt* stmt;
    result = sqlite3_prepare_v2(db, "SELECT key FROM key_names;",
                                -1, &stmt, nullptr);
    checkSQLiteResultOKReturnFalse(result);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      assert(sqlite3_column_count(stmt) == 1);

      auto size = sqlite3_column_bytes(stmt, 0);
      auto text = (const char*) sqlite3_column_text(stmt, 0);

      keys_out.push_back(KeyType(text, size));
    }

    sqlite3_finalize(stmt);

    return true;
  }
  
  bool getKeysWithResult(std::vector<KeyType> &keys_out, std::vector<Result> &results_out, std::string* error_out) override {
    
    assert(delegate != nullptr);
    std::lock_guard<std::mutex> guard(dbMutex);
    
    if (!open(error_out))
      return false;
    
    auto stmt = getKeysWithResultStmt;
    
    int result = sqlite3_reset(stmt);
    checkSQLiteResultOKReturnFalse(result);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      assert(sqlite3_column_count(stmt) == 9);
      
      auto dbKeyID = DBKeyID(sqlite3_column_int64(stmt, 0));
      auto key = KeyType((const char *)sqlite3_column_text(stmt, 1), sqlite3_column_bytes(stmt, 1));
      
      auto engineKeyID = delegate->getKeyID(key);
      engineKeyIDs[dbKeyID] = engineKeyID;
      dbKeyIDs[engineKeyID] = dbKeyID;
      
      Result result;
      int numValueBytes = sqlite3_column_bytes(stmt, 2);
      result.value.resize(numValueBytes);
      memcpy(result.value.data(),
             sqlite3_column_blob(stmt, 2),
             numValueBytes);
      result.builtAt = sqlite3_column_int64(stmt, 3);
      result.computedAt = sqlite3_column_int64(stmt, 4);
      result.start = sqlite3_column_double(stmt, 5);
      result.end = sqlite3_column_double(stmt, 6);
      auto numDependencyBytes = sqlite3_column_bytes(stmt, 7);
      auto dependencyBytes = sqlite3_column_blob(stmt, 7);
      
      // map dependencies
      int numDependencies = numDependencyBytes / sizeof(uint64_t);
      if (numDependencyBytes != static_cast<int>(numDependencies * sizeof(uint64_t))) {
        *error_out = (llvm::Twine("unexpected contents for database result: ") +
                      llvm::Twine((int)dbKeyID.value)).str();
        return false;
      }

      result.dependencies.resize(numDependencies);
      basic::BinaryDecoder decoder(
                                   StringRef((const char*)dependencyBytes, numDependencyBytes));
      for (auto i = 0; i != numDependencies; ++i) {
        uint64_t raw;
        decoder.read(raw);
        bool orderOnly = raw & 1;
        bool singleUse = (raw >> 1) & 1;
        DBKeyID dbKeyID(raw >> 2);
        
        // Map the database key ID into an engine key ID (note that we already
        // hold the dbMutex at this point as required by getKeyIDforID())
        KeyID keyID = getKeyIDForID(dbKeyID, error_out);
        if (!error_out->empty()) {
          return false;
        }
        result.dependencies.set(i, keyID, orderOnly, singleUse);
      }
      
      result.signature = basic::CommandSignature(sqlite3_column_int64(stmt, 8));
      
      keys_out.push_back(key);
      results_out.push_back(result);
    }
    
    return true;
  }

  virtual void dump(raw_ostream& os) override {
    std::lock_guard<std::mutex> guard(dbMutex);

    std::string error;
    if (!open(&error)) {
      os << "error: " << getCurrentErrorMessage() << "\n";
      return;
    }

    // Dump Keys
    int result;
    sqlite3_stmt* stmt;
    result = sqlite3_prepare_v2(db, "SELECT key, id FROM key_names;",
                                -1, &stmt, nullptr);
    if (result != SQLITE_OK)
      return;

    os << "keys:\n";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      assert(sqlite3_column_count(stmt) == 2);

      auto size = sqlite3_column_bytes(stmt, 0);
      auto text = (const char*) sqlite3_column_text(stmt, 0);

      auto id = sqlite3_column_int64(stmt, 1);

      os << id << " -- " << KeyType(text, size).c_str() << "\n";
    }

    sqlite3_finalize(stmt);

    // Dump Keys
    result = sqlite3_prepare_v2(db, "SELECT key_id, built_at, computed_at, start, end FROM rule_results;",
                                -1, &stmt, nullptr);
    if (result != SQLITE_OK)
      return;

    os << "\nresults:\n";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      assert(sqlite3_column_count(stmt) == 5);

      auto id = sqlite3_column_int64(stmt, 0);
      auto built = sqlite3_column_int64(stmt, 1);
      auto computed = sqlite3_column_int64(stmt, 2);
      auto start = sqlite3_column_double(stmt, 3);
      auto end = sqlite3_column_double(stmt, 4);

      os << id << " -- " << built << ", " << computed << ", " << end-start << "s\n";
    }

    sqlite3_finalize(stmt);
  }

  /// @}

  
private:
  /// Local cache of database DBKeyID (values) to engine KeyIDs
  llvm::DenseMap<DBKeyID, KeyID> engineKeyIDs;

  /// Local cache of database engine KeyIDs to DBKeyIDs
  llvm::DenseMap<KeyID, DBKeyID> dbKeyIDs;

  /// Lookup or create a DBKeyID for a given engine KeyID
  ///
  /// This method is not thread-safe. The caller must protect access via the
  /// dbMutex.
  DBKeyID getKeyID(KeyID keyID, std::string *error_out) {
    // Try to fetch the DBKeyID from the cache
    auto it = dbKeyIDs.find(keyID);
    if (it != dbKeyIDs.end()) {
      return it->second;
    }

    auto dbKeyID = getKeyIDFromDB(keyID, error_out);

    if (dbKeyID.value != 0) {
      // Cache the ID mappings
      engineKeyIDs[dbKeyID] = keyID;
      dbKeyIDs[keyID] = dbKeyID;
    }

    return dbKeyID;
  }

  // Helper function that searches and updates the key_names table as needed to
  // return a DBKeyID for the given engine KeyID. This should really only be
  // used by the above cached getKeyID() method.
  DBKeyID getKeyIDFromDB(KeyID keyID, std::string *error_out) {
#define checkSQLiteResultOKReturnDBKeyID(result) \
if (result != SQLITE_OK) { \
  *error_out = getCurrentErrorMessage(); \
  return DBKeyID(); \
}

    int result;

    // Search for the key in the key_names table
    auto key = delegate->getKeyForID(keyID);
    result = sqlite3_reset(findKeyIDForKeyStmt);
    checkSQLiteResultOKReturnDBKeyID(result);
    result = sqlite3_clear_bindings(findKeyIDForKeyStmt);
    checkSQLiteResultOKReturnDBKeyID(result);
    result = sqlite3_bind_text(findKeyIDForKeyStmt, /*index=*/1,
                               key.data(), key.size(),
                               SQLITE_STATIC);
    checkSQLiteResultOKReturnDBKeyID(result);

    result = sqlite3_step(findKeyIDForKeyStmt);
    if (result == SQLITE_ROW) {
      assert(sqlite3_column_count(findKeyIDForKeyStmt) == 1);

      // Found a keyID.
      return DBKeyID(sqlite3_column_int64(findKeyIDForKeyStmt, 0));
    }

    // Did not find the key, need to insert.
    result = sqlite3_reset(insertIntoKeysStmt);
    checkSQLiteResultOKReturnDBKeyID(result);
    result = sqlite3_clear_bindings(insertIntoKeysStmt);
    checkSQLiteResultOKReturnDBKeyID(result);
    result = sqlite3_bind_text(insertIntoKeysStmt, /*index=*/1,
                               key.data(), key.size(),
                               SQLITE_STATIC);
    checkSQLiteResultOKReturnDBKeyID(result);
    result = sqlite3_step(insertIntoKeysStmt);
    if (result != SQLITE_DONE) {
      *error_out = getCurrentErrorMessage();
      return DBKeyID();
    }

    return DBKeyID(sqlite3_last_insert_rowid(db));
#undef checkSQLiteResultOKReturnDBKeyID
  }

  /// Maps a DBKeyID into an engine KeyID
  ///
  /// This method is not thread-safe. The caller must protect access via the
  /// dbMutex.
  KeyID getKeyIDForID(DBKeyID dbKeyID, std::string *error_out) {
#define checkSQLiteResultOKReturnKeyID(result) \
if (result != SQLITE_OK) { \
  *error_out = getCurrentErrorMessage(); \
  return KeyID(); \
}

    // Search local db <-> engine mapping cache
    auto it = engineKeyIDs.find(dbKeyID);
    if (it != engineKeyIDs.end())
      return it->second;

    // Search for the key in the database
    int result;
    result = sqlite3_reset(findKeyNameForKeyIDStmt);
    checkSQLiteResultOKReturnKeyID(result);
    result = sqlite3_clear_bindings(findKeyNameForKeyIDStmt);
    checkSQLiteResultOKReturnKeyID(result);
    result = sqlite3_bind_int64(findKeyNameForKeyIDStmt, /*index=*/1, dbKeyID.value);
    checkSQLiteResultOKReturnKeyID(result);

    result = sqlite3_step(findKeyNameForKeyIDStmt);
    if (result != SQLITE_ROW) {
      *error_out = getCurrentErrorMessage();
      return KeyID();
    }
    assert(sqlite3_column_count(findKeyNameForKeyIDStmt) == 1);

    // Found a key
    auto size = sqlite3_column_bytes(findKeyNameForKeyIDStmt, 0);
    auto text = (const char*) sqlite3_column_text(findKeyNameForKeyIDStmt, 0);

    // Map the key to an engine ID
    auto engineKeyID = delegate->getKeyID(KeyType(text, size));

    // Cache the mapping locally
    engineKeyIDs[dbKeyID] = engineKeyID;
    dbKeyIDs[engineKeyID] = dbKeyID;

    return engineKeyID;
#undef checkSQLiteResultOKReturnKeyID
  }
};
  
}

std::unique_ptr<BuildDB> core::createSQLiteBuildDB(StringRef path,
                                                   uint32_t clientSchemaVersion,
                                                   bool recreateUnmatchedVersion,
                                                   std::string *error_out) {
  return llvm::make_unique<SQLiteBuildDB>(path, clientSchemaVersion, recreateUnmatchedVersion);
}

#undef checkSQLiteResultOKReturnFalse
