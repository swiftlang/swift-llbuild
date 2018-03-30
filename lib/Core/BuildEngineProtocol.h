//===- BuildEngineProtocol.h ------------------------------------*- C++ -*-===//
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
//
// This file defines the codable structures which represent the binary protocol
// used by the \see BuildEngineServer and \see BuildEngineClient.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_CORE_BUILDENGINEPROTOCOL_H
#define LLBUILD_CORE_BUILDENGINEPROTOCOL_H

#include "llbuild/Basic/BinaryCoding.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/StringRef.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace llbuild {
namespace core {
namespace engine_protocol {

/// A constant defining the current protocol version.
///
/// This should be changed any time the protocol is extended.
///
/// Version History:
///
/// 0: Initial revision. 
const uint32_t kBuildEngineProtocolVersion = 0;

// MARK: Message Types

// The basic protocol consists of objects encoded using the BinaryCoding
// interface (values are little-endian, for example).
//
// The protocol is a sequence of messages:
//
//   frame := size :: kind :: message
//   message := kind :: ... body ...
//   size := <uint32_t>
//   kind := <uint32_t>

enum class MessageKind {
  /// Announce a client, this should always be the first message from a client.
  AnnounceClient = 0,
};

struct AnnounceClient {
  static const auto messageKind = engine_protocol::MessageKind::AnnounceClient;
  
  /// The protocol version in use by the client, \see
  /// kBuildEngineProtocolVersion.
  uint32_t protocolVersion;

  /// The task ID the client is registering as.
  std::string taskID;
};

// MARK: Message IO

/// A abstract generic IO interface to a message-based socket.
///
/// This implementation is currently thread based, it is not particularly
/// efficient.
///
/// This class is *NOT* thread-safe.
class MessageSocket {
  /// The socket file descriptor.
  int fd;

  /// Whether the connection is terminating.
  std::atomic<bool> isTerminating{ false };

  /// The reader thread.
  std::unique_ptr<std::thread> readerThread;

  /// Write a sequence of bytes to the output socket.
  void writeBytes(StringRef data);

  /// Read and dispatch messages.
  void readMessages();

public:
  /// Create a new socket for handling messages on the given file descriptor.
  MessageSocket(int fd);
  virtual ~MessageSocket();

  /// Start processing messages from the connect.
  void resume();
  
  /// Force the connection to shutdown.
  void shutdown();
  
  /// Enqueue a new message to the socket.
  ///
  /// The write is done synchronously, this may block.
  //
  // FIXME: Change this to async, probably.
  template<typename T>
  void writeMessage(T&& msg);
      
  /// Handle a received message.
  ///
  /// This entry point is called synchronously from the IO handler, it should
  /// generally either be very fast or defer work to a separate thread.
  ///
  /// NOTE: This cannot be a pure virtual function, it may be called
  /// synchronously with the destructor.
  ///
  /// \parameter kind The kind of the message that was received.
  /// \parameter data The complete data payload.
  virtual void handleMessage(MessageKind kind, StringRef data) {}
};

}
}

template<typename T>
void core::engine_protocol::MessageSocket::writeMessage(T&& msg) {
    // Encode the message with reserved space for the size (backpatched below).
  basic::BinaryEncoder coder{};
    coder.write((uint32_t)0);
    coder.write(T::messageKind);
    coder.write(msg);
    auto contents = coder.contents();

    // Backpatch the size.
    //
    // FIXME: If we don't go the streaming route, we could extend BinaryCoding
    // to support in-place coding which would make this more elegant.
    uint32_t size = contents.size() - 8;
    contents[0] = uint8_t(size >> 0);
    contents[1] = uint8_t(size >> 8);
    contents[2] = uint8_t(size >> 16);
    contents[3] = uint8_t(size >> 24);

    writeBytes(StringRef(
                   reinterpret_cast<char*>(contents.data()),
                   contents.size()));
}

// MARK: BinaryCoding Traits
 
template<>
struct basic::BinaryCodingTraits<core::engine_protocol::MessageKind> {
  typedef core::engine_protocol::MessageKind MessageKind;
  
  static inline void encode(MessageKind& value, BinaryEncoder& coder) {
    uint32_t tmp = uint32_t(value);
    assert(value == MessageKind(tmp));
    coder.write(tmp);
  }
  static inline void decode(MessageKind& value, BinaryDecoder& coder) {
    uint32_t tmp;
    coder.read(tmp);
    value = MessageKind(tmp);
  }
};
 
template<>
struct basic::BinaryCodingTraits<core::engine_protocol::AnnounceClient> {
  typedef core::engine_protocol::AnnounceClient T;
  
  static inline void encode(T& value, BinaryEncoder& coder) {
    coder.write(value.protocolVersion);
    coder.write(value.taskID);
  }
  static inline void decode(T& value, BinaryDecoder& coder) {
    coder.read(value.protocolVersion);
    coder.read(value.taskID);
  }
};

}

#endif
