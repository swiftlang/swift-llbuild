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

using namespace llbuild;
using namespace llbuild::ninja;

bool Rule::isValidParameterName(const std::string& Name) {
  return Name == "command" ||
    Name == "description" ||
    Name == "deps" ||
    Name == "depfile" ||
    Name == "generator" ||
    Name == "pool" ||
    Name == "restat" ||
    Name == "rspfile" ||
    Name == "rspfile_content";
}

Manifest::Manifest() {
  // Create the built-in phony rule, and add it to the rule map.
  PhonyRule = new Rule("phony");
  Rules["phony"].reset(PhonyRule);
}

Node* Manifest::getOrCreateNode(const std::string& Path) {
  // If we have an existing node for this path, return it.
  auto it = Nodes.find(Path);
  if (it != Nodes.end())
    return it->second.get();

  Node *Result = new Node(Path);
  Nodes[Path].reset(Result);
  return Result;
}
