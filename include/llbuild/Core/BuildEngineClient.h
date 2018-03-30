//===- BuildEngineClient.h --------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_BUILDENGINECLIENT_H
#define LLBUILD_CORE_BUILDENGINECLIENT_H

#include "llbuild/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace llbuild {
namespace core {

/// A build engine client is used to connect to a build engine service, and
/// exposes an API similar to the native build engine API, but which can be used
/// from an external client.
class BuildEngineClient {
  void *impl;

public:
  /// Create a client to connect to the server hosted at the given UNIX domain
  /// socket path.
  ///
  /// \parameter path - The path to the socket.
  /// \parameter taskID - The task ID that was provided to this client to
  /// register with.
  explicit BuildEngineClient(StringRef path, StringRef taskID);
  ~BuildEngineClient();

  /// Connect to the server.
  ///
  /// \parameter error_out [out] On error, a message describing the problem.
  /// \returns True on success.
  bool connect(std::string* error_out);

  /// Disconnect from the server.
  void disconnect();
};

}
}

#endif
