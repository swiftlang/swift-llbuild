//===-- BuildDescription.cpp ----------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/BuildSystem/BuildDescription.h"

#include "llbuild/BuildSystem/Command.h"
#include "llbuild/BuildSystem/Tool.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

Node::~Node() {}

Command::~Command() {}

basic::CommandSignature Command::getSignature() const {
  return basic::CommandSignature().combine(name);
}


Tool::~Tool() {}

std::unique_ptr<Command> Tool::createCustomCommand(const BuildKey& key) {
  return nullptr;
}

Node& BuildDescription::addNode(std::unique_ptr<Node> value) {
  auto& result = *value.get();
  getNodes()[value->getName()] = std::move(value);
  return result;
}

Target& BuildDescription::addTarget(std::unique_ptr<Target> value) {
  auto& result = *value.get();
  getTargets()[value->getName()] = std::move(value);
  return result;
}

Command& BuildDescription::addCommand(std::unique_ptr<Command> value) {
  auto& result = *value.get();
  getCommands()[value->getName()] = std::move(value);
  return result;
}

Tool& BuildDescription::addTool(std::unique_ptr<Tool> value) {
  auto& result = *value.get();
  getTools()[value->getName()] = std::move(value);
  return result;
}
