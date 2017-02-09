//===-- BuildDescription.cpp ----------------------------------------------===//
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

#include "llbuild/BuildSystem/BuildDescription.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

Node::~Node() {}

Command::~Command() {}

Tool::~Tool() {}

std::unique_ptr<Command> Tool::createCustomCommand(const BuildKey& key) {
  return nullptr;
}
