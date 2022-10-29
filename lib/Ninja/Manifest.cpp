//===-- Manifest.cpp ------------------------------------------------------===//
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

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

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
  assert(consolePool != nullptr);
  if (consolePool)
    consolePool->setDepth(1);
  pools["console"] = consolePool;

  // Create the built-in phony rule, and add it to the rule map.
  phonyRule = new (getAllocator()) Rule("phony");
  getRootScope().getRules()["phony"] = phonyRule;
}

bool Manifest::normalize_path(StringRef workingDirectory, SmallVectorImpl<char>& tmp){
  auto separatorRef = llvm::sys::path::get_separator();
  assert(separatorRef.size() == 1);
  char slash = *separatorRef.data();

    llvm::sys::fs::make_absolute(workingDirectory, tmp);
  if (tmp.size() == 0 || !llvm::sys::path::is_absolute(tmp)) {
      return false;
  }

  // Ignore root "network" prefixes, such as "//net/".
  StringRef tmpRef{tmp.data(), tmp.size()};
  auto rootNameLength = llvm::sys::path::root_name(tmpRef).size();

  auto begin = tmp.begin() + rootNameLength;
  auto src_it = begin;
  auto dst_it = begin;
  auto end = tmp.end();
  for (; src_it < end; ++src_it) {
    if (LLVM_LIKELY(*src_it != slash)) {
        *dst_it++ = *src_it;
        continue;
    }

    if (dst_it == begin || *(dst_it-1) != slash) {
      *dst_it++ = slash;
    }

    // Current fragment "/"
    if (LLVM_UNLIKELY(src_it + 1 >= end)) {
      break;
    }

    if (LLVM_LIKELY(*(src_it + 1) != '.')) {
      continue;
    }

    // Current fragment "/."
    if (src_it + 2 >= end) {
      ++src_it;
      continue;
    }

    if (*(src_it + 2) != '.') {
      if (*(src_it + 2) == slash) {
        // Means "/./"
        ++src_it;
      } else {
        // "/.?"
        *dst_it++ = *++src_it;
      }
      continue;
    }

    if (src_it + 3 < end && *(src_it + 3) != slash) {
      continue;
    }

    // Move destination pointer to the previous directory.
    if (dst_it - begin <= 1) {
      // Already at the top.
    } else {
      for (dst_it -= 2; dst_it > begin; --dst_it) {
        if (LLVM_UNLIKELY(*dst_it == slash)) {
          ++dst_it;
          break;
        }
      }
      if (dst_it == begin) {
        *dst_it++ = slash;
      }
    }

    if (src_it + 3 < end) {
      src_it += 2;
    } else {
      break;
    }
  }

  tmp.resize(dst_it - tmp.begin());

  return true;
}

Node* Manifest::findNode(StringRef workingDirectory, StringRef path0) {
  SmallString<256> absPathTmp = path0;
  if (!normalize_path(workingDirectory, absPathTmp)) {
    return nullptr;
  }

  auto it = nodes.find(absPathTmp);
  if (it == nodes.end()) {
    return nullptr;
  }
  return it->second;
}

Node* Manifest::findOrCreateNode(StringRef workingDirectory, StringRef path0) {
  SmallString<256> absPathTmp = path0;
  if (!normalize_path(workingDirectory, absPathTmp)) {
    return nullptr;
  }

  StringRef path = absPathTmp;

  auto& result = nodes[path];
  if (!result)
    result = new (getAllocator()) Node(path, path0);
  return result;
}
