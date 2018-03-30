//===- BuildEngineServer.h --------------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_CORE_BUILDENGINESERVER_H
#define LLBUILD_CORE_BUILDENGINESERVER_H

#include "llbuild/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"

#include <string>

namespace llbuild {
namespace core {

class BuildEngine;

/// A build engine server can be used to expose a server-based interface to a
/// build engine.
///
/// This interface will allow external clients to act as if they are natively
/// integrated with the build engine, by connecting to the server (as part of a
/// particular outstanding task), exposing available rules, and making engine
/// API calls just like a native client.
class BuildEngineServer {
  void *impl;

public:
  /// Create a server for the given engine, using a UNIX domain socket.
  ///
  /// It is a programmatic error to register more than one server for a
  /// particular engine.
  ///
  /// \parameter path The path to the socket.
  explicit BuildEngineServer(BuildEngine& engine, StringRef path);
  ~BuildEngineServer();

  /// Start the server and listen for connections.
  ///
  /// This will listen for connections until the server is explicitly shutdown.
  ///
  /// \parameters error_out [out] On failure, a message describing the problem.
  /// \returns True on success.
  bool start(std::string* error_out);

  /// Shut down the server.
  ///
  /// This will *NOT* attempt to gracefully close the connections to any
  /// clients.
  void shutdown();
};

}
}

#endif
