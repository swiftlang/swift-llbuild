//===-- BuildSystem-C-API-Private.h ---------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2019 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef BuildSystem_C_API_Private_h
#define BuildSystem_C_API_Private_h

#include <llbuild/llbuild.h>

#include "llbuild/BuildSystem/BuildKey.h"

using namespace llbuild::buildsystem;
using namespace llbuild::core;

namespace {
static llb_build_key_kind_t internalToPublicBuildKeyKind(const BuildKey::Kind kind) {
  switch (kind) {
    case BuildKey::Kind::Command:
      return llb_build_key_kind_command;
    case BuildKey::Kind::CustomTask:
      return llb_build_key_kind_custom_task;
    case BuildKey::Kind::DirectoryContents:
      return llb_build_key_kind_directory_contents;
    case BuildKey::Kind::FilteredDirectoryContents:
      return llb_build_key_kind_filtered_directory_contents;
    case BuildKey::Kind::DirectoryTreeSignature:
      return llb_build_key_kind_directory_tree_signature;
    case BuildKey::Kind::DirectoryTreeStructureSignature:
      return llb_build_key_kind_directory_tree_structure_signature;
    case BuildKey::Kind::Node:
      return llb_build_key_kind_node;
    case BuildKey::Kind::Stat:
      return llb_build_key_kind_stat;
    case BuildKey::Kind::Target:
      return llb_build_key_kind_target;
    case BuildKey::Kind::Unknown:
      return llb_build_key_kind_unknown;
  }
}

}
#endif
