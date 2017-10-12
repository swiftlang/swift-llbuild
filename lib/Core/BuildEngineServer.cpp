//===-- BuildEngineServer.cpp ---------------------------------------------===//
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

#include "llbuild/Core/BuildEngineServer.h"

#include "llbuild/Basic/LLVM.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ErrorHandling.h"

#include <errno.h>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace llbuild;
using namespace llbuild::core;

namespace {

class BuildEngineServerImpl {
  /// The engine the server is for.
  BuildEngine& engine;
  
  /// The path to the UNIX domain socket to connect on.
  std::string path;

  /// Whether the server is connected.
  bool isConnected = false;

  /// The socket file descriptor.
  int socketFD = -1;

  /// The thread responsible for handling the client connections.
  std::unique_ptr<std::thread> serverThread;

  /// Serve connections until shutdown.
  void serve() {
    while (true) {
      int fd = ::accept(socketFD, nullptr, nullptr);
      if (fd < 0) {
        // If the connection has been aborted, we have been terminated.
        //
        // FIXME: This isn't safe, there is a race here.
        if (errno == EBADF || errno == ECONNABORTED) break;

        // Otherwise, we don't know how to handle this error.
        llvm::report_fatal_error("unexpected error accepting connections");
      }

      // Spawn a thread to handle this connection.
      //
      // FIXME: This isn't efficient, we want a select loop.
      auto t = new std::thread(&BuildEngineServerImpl::serveClient, this, fd);
      t->detach();
    }
  }

  /// Service an individual client connection.
  void serveClient(int fd) {
    while (true) {
      uint8_t buf[4096];
      auto n = read(fd, buf, sizeof(buf));
      if (n < 0) {
        // If we experience errors on this connection, there is nothing we can
        // do.
        close(fd);
        break;
      }

      if (n == 0) {
        close(fd);
        break;
      }

      // FIXME: We read data, service the connection.
    }
  }

public:
  BuildEngineServerImpl(BuildEngine& engine, StringRef path)
      : engine(engine), path(path) {}
  ~BuildEngineServerImpl() {
    // If the server is connected, shut it down now.
    if (isConnected) {
      shutdown();
    }
  }

  bool start(std::string* error_out) {
    assert(!isConnected && "client is already connected");

    // Create the socket.
    socketFD = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFD < 0) {
      *error_out = std::string("unable to open socket: ") + strerror(errno);
      return false;
    }

    // Bind the socket.
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
    if (::bind(socketFD, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      *error_out = std::string("unable to bind socket: ") + strerror(errno);
      goto fail;
    }

    // Begin listening for connections.
    if (::listen(socketFD, 8) < 0) {
      *error_out = std::string("unable to listen on socket: ") +
        strerror(errno);
      goto fail;
    }

    // If we reached this far, we are connected.
    //
    // Spawn off a thread to manage the connection.
    serverThread = llvm::make_unique<std::thread>(
        &BuildEngineServerImpl::serve, this);

    isConnected = true;
    return true;

  fail:
    shutdown(true);
    return false;
  }

  void shutdown(bool force = false) {
    assert((force || isConnected) && "client is disconnected");
    (void)engine;

    // We currently cause the server thread to shutdown by closing its
    // socket.
    //
    // FIXME: This is racy and not safe. We can fix this once we move to a
    // select loop.
    
    if (socketFD >= 0) {
      (void)close(socketFD);
      (void)unlink(path.c_str());
      socketFD = -1;
    }

    // Wait for the server to terminate.
    if (serverThread) {
      serverThread->join();
    }

    isConnected = false;
  }
};
  
}

BuildEngineServer::BuildEngineServer(BuildEngine& engine, StringRef path)
    : impl(new BuildEngineServerImpl(engine, path)) 
{
}

BuildEngineServer::~BuildEngineServer() {
  delete static_cast<BuildEngineServerImpl*>(impl);
}

bool BuildEngineServer::start(std::string* error_out) {
  return static_cast<BuildEngineServerImpl*>(impl)->start(error_out);
}

void BuildEngineServer::shutdown() {
  static_cast<BuildEngineServerImpl*>(impl)->shutdown();
}
