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

#include <cstdint>

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
  virtual void setCurrentIteration(uint64_t Value) = 0;

  /// Look up the result for a rule.
  ///
  /// \param Rule The rule to look up the result for.
  /// \param Result_Out [out] The result, if found.
  /// \returns True if the database had a stored result for the rule.
  //
  // FIXME: This might be more efficient if it returns Result.
  //
  // FIXME: Figure out if we want a more lazy approach where we make the
  // database cache result objects and we query them only when needed. This may
  // scale better to very large build graphs.
  virtual bool lookupRuleResult(const Rule& Rule, Result* Result_Out) = 0;

  /// Update the stored result for a rule.
  virtual void setRuleResult(const Rule& Rule, const Result& Result) = 0;

  /// Called by the build engine to indicate a build has finished, and results
  /// should be written.
  virtual void buildComplete() = 0;
};

}
}

#endif
