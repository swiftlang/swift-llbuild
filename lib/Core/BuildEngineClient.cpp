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

#include "llbuild/Basic/LLVM.h"

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace llbuild;
using namespace llbuild::core;

namespace {

class BuildEngineClientImpl {
  /// The path to the UNIX domain socket to connect on.
  std::string path;

  /// The taskID to register with.
  std::string taskID;

  /// Whether the client is connected.
  bool isConnected = false;

  /// The socket file descriptor.
  int socketFD = -1;
  
public:
  BuildEngineClientImpl(StringRef path, StringRef taskID)
      : path(path), taskID(taskID) {}

  bool connect(std::string* error_out) {
    assert(!isConnected && "client is already connected");

    // Create the socket.
    socketFD = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFD < 0) {
      *error_out = std::string("unable to open socket: ") + strerror(errno);
      return false;
    }

    // Connect the socket.
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
    if (::connect(socketFD, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      *error_out = std::string("unable to connect socket: ") + strerror(errno);
      goto fail;
    }

    (void)::write(socketFD, "1", 1);
    isConnected = true;
    
    return true;

  fail:
    disconnect(true);
    return false;
  }

  void disconnect(bool force = true) {
    assert((force || isConnected) && "client is disconnected");
    if (socketFD >= 0) {
      (void)::close(socketFD);
      socketFD = -1;
    }
    isConnected = false;
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
