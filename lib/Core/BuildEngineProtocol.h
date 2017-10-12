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

#include <cstdint>
#include <string>

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

}
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
