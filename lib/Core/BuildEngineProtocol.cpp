//===- BuildEngineProtocol.cpp --------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "BuildEngineProtocol.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"

#include <thread>

#include <sys/socket.h>
#include <unistd.h>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::core;

core::engine_protocol::MessageSocket::MessageSocket(int fd)
    : fd(fd), readerThread(nullptr)
{
}

core::engine_protocol::MessageSocket::~MessageSocket() {
  shutdown();
}

void core::engine_protocol::MessageSocket::resume() {
  readerThread.reset(new std::thread(&MessageSocket::readMessages, this));
}

void core::engine_protocol::MessageSocket::shutdown() {
  isTerminating = true;
  
  // Close the socket, this will cause the reader thread to abort any blocked
  // work, and notice we are terminating.
  (void)::close(fd);
  
  // FIXME: Ensure graceful shutdown of the socket.
  if (readerThread) {
    readerThread->join();
    readerThread.reset();
  }
}

void core::engine_protocol::MessageSocket::readMessages() {
  // Our scratch buffer.
  llvm::SmallVector<char, 4096> buf;

  // FIXME: There is a rather subtle race condition here, where if the socket fd
  // is ever re-allocated in between when it is closed, and when we detect we
  // are terminating, then we could effectively deadlock (if the FD was
  // reallocated to be something that would block whatever the next call was).

  while (!isTerminating) {
    // Read onto the end of our buffer.
    const auto readSize = 4096;
    buf.reserve(buf.size() + readSize);
    auto n = ::read(fd, buf.begin() + buf.size(), readSize);
    if (n < 0) {
      // If the connection is terminating, this is expected.
      if (isTerminating)
        break;

      // FIXME: Error handling.
      llvm::report_fatal_error(
          std::string("unexpected read error from socket: ") + strerror(errno));
    }

    if (n == 0) {
      break;
    }

    // Adjust the buffer size.
    buf.set_size(buf.size() + n);

    // Consume all of the messages in the buffer; \see BinaryEngineProtocol.h
    // for the framing definition.
    auto pos = buf.begin();
    const auto headerSize = 8;
    while (buf.end() - pos >= headerSize) {
      // Decode the header.
      uint32_t size;
      engine_protocol::MessageKind kind;
      {
        BinaryDecoder coder(StringRef(pos, headerSize));
        coder.read(size);
        coder.read(kind);
        assert(coder.isEmpty());
      }

      // If we don't have the complete message, we are done.
      if (buf.end() - pos < headerSize + size) break;

      // Process the message.
      handleMessage(kind, StringRef(pos + headerSize, size));
      
      pos += headerSize + size;
    }

    // Drop all read messages.
    buf.erase(buf.begin(), pos);
  }
}

void core::engine_protocol::MessageSocket::writeBytes(StringRef data) {
    auto pos = data.begin();
    while (pos < data.end()) {
      auto bytesRemaining = data.end() - pos;
      auto n = ::write(fd, &*pos, bytesRemaining);
      if (n < 0) {
        // FIXME: Error handling.
        llvm::report_fatal_error("unexpected failure writing to client");
      }
      assert(n <= bytesRemaining);
      pos += n;
    }
}
