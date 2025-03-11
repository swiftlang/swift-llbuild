//===- CASTree.h ------------------------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD3_CASTREE_H
#define LLBUILD3_CASTREE_H

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <llbuild3/Result.hpp>

#include "llbuild3/CAS.h"
#include "llbuild3/CASTree.pb.h"
#include "llbuild3/Error.pb.h"


namespace llbuild3 {

struct NamedDirectoryEntryID {
  NamedDirectoryEntry info;
  CASID id;
};

class CASTree {
private:
  std::shared_ptr<CASDatabase> db;

  CASObject object;
  FileInformation fileInfo;
  CASID id;

public:
  CASTree(const std::vector<NamedDirectoryEntryID>& entries,
          std::shared_ptr<CASDatabase> db);

  inline const FileInformation& info() const { return fileInfo; }
  inline const CASID& treeID() const { return id; }

  void sync(std::function<void (result<CASID, Error>)>);
};


}

#endif
