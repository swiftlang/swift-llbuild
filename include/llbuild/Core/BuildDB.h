//===- BuildDB.h ------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_BUILDDB_H
#define LLBUILD_CORE_BUILDDB_H

#include "llbuild/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"

#include "llbuild/Core/BuildEngine.h"

#include <cstdint>
#include <memory>
#include <string>

namespace llbuild {
namespace core {

struct Result;
class Rule;

/// Delegate interface for use with the build database
class BuildDBDelegate {
public:
  virtual ~BuildDBDelegate();

  /// Get a unique ID for the given key.
  ///
  /// This method must be thread safe.
  ///
  /// The IDs returned by this method must be stable for the lifetime of the
  /// BuildDB instance with which the delegate has been attached. BuildDB
  /// implementations may (and do) cache these values for future use.
  ///
  /// \param key [out] The key whose unique ID is being returned.
  virtual KeyID getKeyID(const KeyType& key) = 0;

  /// Get the key corresponding to a key ID.
  ///
  /// This method must be thread safe, and must not fail.
  virtual KeyType getKeyForID(KeyID key) = 0;
};


class BuildDB {
public:
  virtual ~BuildDB();

  /// Set the delegate for key/id mapping
  ///
  /// \param delegate The delegate pointer (does not take ownership)
  virtual void attachDelegate(BuildDBDelegate* delegate) = 0;

  
  /// Get the current build iteration.
  ///
  /// \param success_out [out] Whether or not the query succeeded.
  /// \param error_out [out] Error string if success_out is false.
  virtual uint64_t getCurrentIteration(bool* success_out, std::string* error_out) = 0;

  /// Set the current build iteration.
  ///
  /// \param error_out [out] Error string if return value is false.
  virtual bool setCurrentIteration(uint64_t value, std::string* error_out) = 0;

  /// Look up the result for a rule.
  ///
  /// \param keyID The keyID for the rule.
  /// \param rule The rule to look up the result for.
  /// \param result_out [out] The result, if found.
  /// \param error_out [out] Error string if an error occurred.
  /// \returns True if the database had a stored result for the rule.
  //
  // FIXME: This might be more efficient if it returns Result.
  //
  // FIXME: Figure out if we want a more lazy approach where we make the
  // database cache result objects and we query them only when needed. This may
  // scale better to very large build graphs.
  inline bool lookupRuleResult(KeyID keyID, const Rule& rule, Result* result_out, std::string* error_out) {
    return lookupRuleResult(keyID, rule.key, result_out, error_out);
  }
  virtual bool lookupRuleResult(KeyID keyID, const KeyType& key, Result* result_out, std::string* error_out) = 0;

  /// Update the stored result for a rule.
  ///
  /// The BuildEngine does not enforce that the dependencies for a Rule are
  /// unique. However, duplicate dependencies have no semantic meaning for the
  /// engine, and the database may elect to discard them from storage.
  ///
  /// The database *MUST*, however, correctly maintain the order of the
  /// dependencies.
  ///
  /// \param keyID The keyID for the rule.
  /// \param error_out [out] Error string if return value is false.
  virtual bool setRuleResult(KeyID keyID, const Rule& rule, const Result& result, std::string* error_out) = 0;

  /// Called by the build engine to indicate that a build has started.
  ///
  /// The engine guarantees that all mutation operations (e.g., \see
  /// setCurrentIteration() and \see setRuleResult()) are only called between
  /// build paired \see buildStarted() and \see buildComplete() calls.
  ///
  /// \param error_out [out] Error string if return value is false.
  virtual bool buildStarted(std::string* error_out) = 0;

  /// Called by the build engine to indicate a build has finished, and results
  /// should be written.
  ///
  /// The expected behavior of the database when \see buildStarted() is called,
  /// but \see buildComplete() is never called (e.g., due to a crash) is not
  /// prescribed. The database implementation may choose to put all
  /// modifications within the scope of a single build in a single transaction,
  /// or it may choose to eagerly commit partial results from the build.
  virtual void buildComplete() = 0;


  /// Get a list of the keys known by the database
  ///
  /// \param keys_out [out] The known keys will be appended to this vector.
  /// \param error_out [out] Error string if return value is false.
  virtual bool getKeys(std::vector<KeyType>& keys_out, std::string* error_out) = 0;

  /// Dump a debug view of the database contents
  virtual void dump(raw_ostream& os) { (void)os; }
};

/// Create a BuildDB instance backed by a SQLite3 database.
///
/// \param clientSchemaVersion An uninterpreted version number for use by the
/// client to allow batch changes to the stored build results; if the stored
/// schema does not match the provided version the database will be cleared upon
/// opening.
std::unique_ptr<BuildDB> createSQLiteBuildDB(StringRef path,
                                             uint32_t clientSchemaVersion,
                                             std::string* error_out);

}
}

#endif
