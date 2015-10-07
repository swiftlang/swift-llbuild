//===-- ManifestLoader.cpp ------------------------------------------------===//
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

#include "llbuild/Ninja/Manifest.h"

#include "llbuild/Basic/LLVM.h"

using namespace llbuild;
using namespace llbuild::ninja;

bool Rule::isValidParameterName(StringRef name) {
  return name == "command" ||
    name == "description" ||
    name == "deps" ||
    name == "depfile" ||
    name == "generator" ||
    name == "pool" ||
    name == "restat" ||
    name == "rspfile" ||
    name == "rspfile_content";
}

Manifest::Manifest() {
  // Create the built-in console pool, and add it to the pool map.
  consolePool = new (getAllocator()) Pool("console");
  consolePool->setDepth(1);
  pools["console"] = consolePool;

  // Create the built-in phony rule, and add it to the rule map.
  phonyRule = new (getAllocator()) Rule("phony");
  rules["phony"] = phonyRule;
}

Node* Manifest::getOrCreateNode(StringRef path) {
  auto& result = nodes[path];
  if (!result)
    result = new (getAllocator()) Node(path);
  return result;
}
