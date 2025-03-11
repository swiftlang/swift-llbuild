//===- CASLog.cpp -----------------------------------------------*- C++ -*-===//
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

#include "llbuild3/CASLog.h"

#include <cassert>
#include <format>

using namespace llbuild3;

namespace {

inline Error makeCASError(CASError code,
                          std::string desc = std::string(),
                          Error* ctx = nullptr) {
  Error err;
  err.set_type(ErrorType::CAS);
  err.set_code(rawCode(code));
  if (!desc.empty()) {
    err.set_description(desc);
  }
  if (ctx) {
    err.add_context()->PackFrom(*ctx);
  }
  return err;
}

}


void CASLogWriter::append(std::string_view data, uint8_t channel,
                          Handler handler) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!isOK) {
      if (handler) handler(fail(makeCASError(CASError::IOError, "stream failed")), true);
      return;
    }
  }

  CASObject obj;
  *obj.mutable_data() = data;
  std::weak_ptr<CASLogWriter> weakSelf(shared_from_this());
  db->put(obj, [weakSelf, channel, handler, size=data.size()](result<CASID, Error> res) {
    if (auto self = weakSelf.lock(); self) {
      self->postChunk(channel, handler, size, res);
    }
  });
}

void CASLogWriter::postChunk(uint8_t channel, Handler handler, uint64_t size,
                             result<CASID, Error> res) {
  if (res.has_error()) {
    if (handler) handler(res, true);
    return;
  }

  auto& contentID = *res;

  uint64_t op = 0;
  std::optional<CASTree> newTree;
  bool isFlush = false;
  {
    std::lock_guard<std::mutex> lock(mutex);

    if (!isOK) {
      // stream failed, error will have already been sent to the handler
      return;
    }

    // create a new entry in the current buffer
    auto index = directoryEntries.size();
    currentSize += size;
    NamedDirectoryEntry entry;
    entry.set_name(std::format("{}.{}", index, channel));
    entry.set_type(FILETYPE_PLAIN_FILE);
    entry.set_size(size);
    directoryEntries.emplace_back(NamedDirectoryEntryID{std::move(entry), contentID});

    // log the pending write operation
    op = ++writeOpCount;
    pendingOps.emplace_back(PendingWriteOp{op, handler});

    if (flushPending) {
      // we are waiting on a flush operation, can't the currenly incomplete
      // buffer yet (no 'root' and 'prev')
    } else if (directoryEntries.size() >= maxEntries) {
      // buffer is both valid and exceeds the target, start a new flush
      flushPending = true;
      isFlush = true;
      newTree = CASTree(directoryEntries, db);
      directoryEntries.clear();
    } else {
      // normal write
      newTree = CASTree(directoryEntries, db);
    }
  }

  if (newTree.has_value()) {
    std::weak_ptr<CASLogWriter> weakSelf(shared_from_this());
    newTree->sync([weakSelf, op, isFlush](result<CASID, Error> res) {
      if (auto self = weakSelf.lock(); self) {
        self->postSync(op, isFlush, res);
      }
    });
  }
}

void CASLogWriter::postSync(uint64_t op, bool isFlush, result<CASID, Error> res) {
  uint64_t chainedOp = 0;
  std::optional<CASTree> chainedTree;
  bool chainedFlush = false;
  {
    std::lock_guard<std::mutex> lock(mutex);

    if (!isFlush) {
      if (op < lastWriteOp) {
        // a later write beat this one, no work to do
        return;
      }

      if (res.has_error()) {
        // write failed, but it wasn't a flush so just notify the client that
        // we've stalled, but will try again
        for (auto it = pendingOps.begin(); it != pendingOps.end(); it++) {
          if ((*it).opIdx == op) {
            auto errCtx = res.error();
            if (auto& handler = (*it).handler; handler) {
              handler(fail(makeCASError(CASError::StreamStall,
                                        "non-fatal write error",
                                        &errCtx)),
                      false);
            }
            return;
          }
        }
        return;
      }

      // successfully wrote the buffer and we're the latest op, move foward
      // the current state
      currentID = *res;
      lastWriteOp = op;

      // update and remove all previous ops
      while (!pendingOps.empty() && pendingOps.front().opIdx <= op) {
        if (pendingOps.front().handler) {
          pendingOps.front().handler(*currentID, true);
        }
        pendingOps.pop_front();
      }
      return;
    } else {
      if (res.has_error()) {
        // a failed flush is catestrophic, as we are not currently keeping the
        // the data around once we've pushed it to the CAS
        isOK = false;

        directoryEntries.clear();

        auto errCtx = res.error();
        auto error = makeCASError(CASError::IOError, "stream failed", &errCtx);
        while (!pendingOps.empty()) {
          if (auto& handler = pendingOps.front().handler; handler) {
            handler(fail(error), true);
          }
          pendingOps.pop_front();
        }

        return;
      }

      // as a flush, we should be the prevailing write, if not badness has
      // ensued
      assert(op > lastWriteOp);

      // move forward the current state
      currentID = *res;
      lastWriteOp = op;

      // first write? record the root of the stream
      if (!rootID.has_value()) {
        rootID = currentID;
        rootSize = currentSize;
      }

      // record previous
      prevID = currentID;
      prevSize = currentSize;

      NamedDirectoryEntry entry;
      entry.set_type(FILETYPE_DIRECTORY);

      entry.set_name("prev");
      entry.set_size(prevSize);
      directoryEntries.emplace(directoryEntries.begin(), NamedDirectoryEntryID{entry, *prevID});

      entry.set_name("root");
      entry.set_size(rootSize);
      directoryEntries.emplace(directoryEntries.begin(), NamedDirectoryEntryID{entry, *rootID});

      // clear out current
      currentID = {};
      currentSize = 0;

      if (directoryEntries.size() > 2) {
        // we accumulated writes while waiting on the flush to complete,
        // set up an internal op to write out the new data
        chainedOp = ++writeOpCount;
        pendingOps.emplace_back(PendingWriteOp{
          chainedOp,
          {}
        });

        if (directoryEntries.size() >= maxEntries) {
          // buffer is both valid and exceeds the target, start a new flush
          flushPending = true;
          chainedFlush = true;
          chainedTree = CASTree(directoryEntries, db);
          directoryEntries.clear();
        } else {
          // normal write
          chainedTree = CASTree(directoryEntries, db);
        }
      }
    }
  }

  if (chainedTree.has_value()) {
    std::weak_ptr<CASLogWriter> weakSelf(shared_from_this());
    chainedTree->sync([weakSelf, chainedOp, chainedFlush](result<CASID, Error> res) {
      if (auto self = weakSelf.lock(); self) {
        self->postSync(chainedOp, chainedFlush, res);
      }
    });
  }
}
