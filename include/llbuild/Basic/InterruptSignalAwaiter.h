//===- InterruptSignalHandler.h ---------------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BASIC_INTERRUPTSIGNALAWAITER_H
#define LLBUILD_BASIC_INTERRUPTSIGNALAWAITER_H

#include <atomic>
#include <functional>
#include <thread>

namespace llbuild {
namespace basic {
/// A platform-independent abstraction that allows users to provide a
/// function that is called when a SIGINT signal is received by the program.
struct InterruptSignalAwaiter {
 private:
  void(*previousSignalHandler)(int);
  static int signalWatchingPipe[2];
  static std::atomic<bool> wasInterrupted;
  std::thread handlerThread;
  std::function<void()> interruptHandler;

  /// Called when a SIGINT is received then sends a message to the
  /// signalWatchingPipe so this class can handle the received signal.
  static void sigintHandler(int);

  /// Constructs the signal awaiter by registering a signal handler, creating
  // the pipe and firing up a thread on which to listen for signals.
  InterruptSignalAwaiter();

  /// Blocking function that waits for indications that signals have arrived
  /// and process them.
  void waitForSignal();

 public:
  /// The object on which to register interrupt handlers. This is a singleton
  /// as we share static state across the class.
  static InterruptSignalAwaiter GlobalAwaiter;

  /// The main use of this class. Sets a function to be called whenever the
  /// program receives a SIGINT signal.
  void setInterruptHandler(std::function<void()> interruptHandler) {
    this->interruptHandler = interruptHandler;
  }

  /// Clears the handler for an interrupt signal.
  void resetInterruptHandler() { setInterruptHandler([] {}); }

  /// Sets the SIGINT handler to the default (SIG_DFL).
  static void resetProgramSignalHandler();

  /// Stops listening for and handling SIGINT signals.
  ~InterruptSignalAwaiter();
};
}
}

#endif  // LLBUILD_BASIC_INTERRUPTSIGNALAWAITER_H
