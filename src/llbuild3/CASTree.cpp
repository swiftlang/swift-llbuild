//===- CASTree.cpp ----------------------------------------------*- C++ -*-===//
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

#include "llbuild3/CASTree.h"

#include <algorithm>
#include <ranges>

using namespace llbuild3;

CASTree::CASTree(const std::vector<NamedDirectoryEntryID>& entries,
                 std::shared_ptr<CASDatabase> db) : db(db) {
  std::vector<std::size_t> indexes(entries.size());
  std::iota(indexes.begin(), indexes.end(), std::size_t{ 0 });
  auto proj = [&entries](std::size_t i) -> const NamedDirectoryEntryID& {
    return entries[i];
  };

  std::ranges::sort(indexes, [](const NamedDirectoryEntryID& l, const NamedDirectoryEntryID& r){
    return l.info.name() < r.info.name();
  }, proj);

  auto sortedView = std::ranges::views::transform(indexes, proj);
  uint64_t aggregateSize = 0;
  NamedDirectoryEntries& dirEntries = *fileInfo.mutable_inlinechildren();
  for (const auto& entry : sortedView) {
    *object.add_refs() = entry.id;
    *dirEntries.add_entries() = entry.info;
    // FIXME: check and handle overflow?
    aggregateSize += entry.info.size();
  }

  fileInfo.set_type(FILETYPE_DIRECTORY);
  fileInfo.set_size(aggregateSize);
  fileInfo.set_compression(FILEDATACOMPRESSIONMETHOD_NONE);

  fileInfo.SerializeToString(object.mutable_data());
  id = db->identify(object);
}

void CASTree::sync(std::function<void (result<CASID, Error>)> handler) {
  db->put(object, [this, handler](result<CASID, Error> res) {
    if (res.has_error()) {
      if (handler) handler(fail(res.error()));
    } else {
      assert(id.bytes() == res->bytes());
      if (handler) handler(*res);
    }
  });
}
