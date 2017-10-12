//===-- BuildEngineClient.cpp ---------------------------------------------===//
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

#include "llbuild/Core/BuildEngineClient.h"

#include "llbuild/Basic/BinaryCoding.h"
#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

#include "BuildEngineProtocol.h"

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::core;

namespace {

class ServerConnection: public engine_protocol::MessageSocket {
public:
  using MessageSocket::MessageSocket;
  
  /// Process an individual client message.
  virtual void handleMessage(engine_protocol::MessageKind kind,
                             StringRef data) {
  }
};

class BuildEngineClientImpl {
  /// The path to the UNIX domain socket to connect on.
  std::string path;

  /// The task ID to register with.
  std::string taskID;

  /// The connection to the server, once established.
  std::unique_ptr<ServerConnection> connection;
  
public:
  BuildEngineClientImpl(StringRef path, StringRef taskID)
      : path(path), taskID(taskID) {}

  bool connect(std::string* error_out) {
    assert(!connection && "client is already connected");

    // Create the socket.
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      *error_out = std::string("unable to open socket: ") + strerror(errno);
      return false;
    }

    // Connect the socket.
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      *error_out = std::string("unable to connect socket: ") + strerror(errno);
      goto fail;
    }

    // Create the server connection.
    connection = llvm::make_unique<ServerConnection>(fd);
    connection->resume();
    
    // Send the introductory message.
    connection->writeMessage(engine_protocol::AnnounceClient{
        engine_protocol::kBuildEngineProtocolVersion, taskID });
    
    return true;

  fail:
    disconnect(true);
    return false;
  }

  void disconnect(bool force = true) {
    assert((force || connection != nullptr) && "client is disconnected");
    connection.reset();
  }
};
  
}

BuildEngineClient::BuildEngineClient(StringRef path, StringRef taskID)
    : impl(new BuildEngineClientImpl(path, taskID)) 
{
}

BuildEngineClient::~BuildEngineClient() {
  delete static_cast<BuildEngineClientImpl*>(impl);
}

bool BuildEngineClient::connect(std::string* error_out) {
  return static_cast<BuildEngineClientImpl*>(impl)->connect(error_out);
}

void BuildEngineClient::disconnect() {
  static_cast<BuildEngineClientImpl*>(impl)->disconnect();
}
