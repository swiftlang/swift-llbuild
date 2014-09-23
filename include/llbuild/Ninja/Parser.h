//===- Parser.h -------------------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_NINJA_PARSER_H
#define LLBUILD_NINJA_PARSER_H

#include "llbuild/Ninja/Lexer.h"

#include <cstdint>
#include <string>

namespace llbuild {
namespace ninja {

/// Delegate interface for parser behavior.
class ParseActions {
public:
  virtual ~ParseActions();

  virtual void ActOnBeginManifest(std::string Name) = 0;
  virtual void ActOnEndManifest() = 0;
};

/// Interface for parsing a Ninja build manifest.
class Parser {
  Lexer Lexer;
  ParseActions &Actions;

public:
  explicit Parser(const char* Data, uint64_t Length,
                  ParseActions &Actions);
  ~Parser();

  void Parse();
};

}
}

#endif
