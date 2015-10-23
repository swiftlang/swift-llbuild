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

#include <cstdint>
#include <memory>
#include <string>

namespace llbuild {
namespace core {

struct Result;
class Rule;

class BuildDB {
public:
  virtual ~BuildDB();

  /// Get the current build iteration.
  virtual uint64_t getCurrentIteration() = 0;

  /// Set the current build iteration.
  virtual void setCurrentIteration(uint64_t value) = 0;

  /// Look up the result for a rule.
  ///
  /// \param rule The rule to look up the result for.
  /// \param result_out [out] The result, if found.
  /// \returns True if the database had a stored result for the rule.
  //
  // FIXME: This might be more efficient if it returns Result.
  //
  // FIXME: Figure out if we want a more lazy approach where we make the
  // database cache result objects and we query them only when needed. This may
  // scale better to very large build graphs.
  virtual bool lookupRuleResult(const Rule& rule, Result* result_out) = 0;

  /// Update the stored result for a rule.
  ///
  /// The BuildEngine does not enforce that the dependencies for a Rule are
  /// uniqu. However, duplicate dependencies have no semantic meaning for the
  /// engine, and the database may elect to discard them for storage.
  virtual void setRuleResult(const Rule& rule, const Result& result) = 0;

  /// Called by the build engine to indicate that a build has started.
  ///
  /// The engine guarantees that all mutation operations (e.g., \see
  /// setCurrentIteration() and \see setRuleResult()) are only called between
  /// build paired \see buildStarted() and \see buildComplete() calls.
  virtual void buildStarted() = 0;

  /// Called by the build engine to indicate a build has finished, and results
  /// should be written.
  virtual void buildComplete() = 0;
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
