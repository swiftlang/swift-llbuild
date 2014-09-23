//===-- Parser.cpp --------------------------------------------------------===//
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

#include "llbuild/Ninja/Parser.h"

#include "llbuild/Ninja/Lexer.h"

using namespace llbuild;
using namespace llbuild::ninja;

#pragma mark - ParseActions

ParseActions::~ParseActions() {
}

#pragma mark - Parser Implementation

class ninja::ParserImpl {
  Lexer Lexer;
  ParseActions &Actions;

public:
  ParserImpl(const char* Data, uint64_t Length,
             ParseActions &Actions);

  void Parse();
};

ParserImpl::ParserImpl(const char* Data, uint64_t Length, ParseActions &Actions)
  : Lexer(Data, Length), Actions(Actions)
{
}

void ParserImpl::Parse() {
  Actions.ActOnBeginManifest("<main>");
  Actions.ActOnEndManifest();
}

#pragma mark - Parser

Parser::Parser(const char* Data, uint64_t Length,
               ParseActions &Actions)
  : Impl(new ParserImpl(Data, Length, Actions))
{
}

Parser::~Parser() {
}

void Parser::Parse() {
  Impl->Parse();
}

