//===-- InterruptSignalAwaiter.cpp ----------------------------------------===//
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

#include "llbuild/Basic/InterruptSignalAwaiter.h"

#include "llbuild/Basic/PlatformUtility.h"

#include <csignal>

using namespace llbuild;
using namespace llbuild::basic;

int InterruptSignalAwaiter::signalWatchingPipe[2]{-1, -1};
std::atomic<bool> InterruptSignalAwaiter::wasInterrupted{false};

InterruptSignalAwaiter InterruptSignalAwaiter::GlobalAwaiter{};

void InterruptSignalAwaiter::sigintHandler(int) {
  // Set the atomic interrupt flag.
  wasInterrupted = true;

  // Write to wake up the signal monitoring thread.
  char byte{};
  sys::write(signalWatchingPipe[1], &byte, 1);
}

InterruptSignalAwaiter::InterruptSignalAwaiter() {
  previousSignalHandler = std::signal(SIGINT, &sigintHandler);

  if (sys::pipe(signalWatchingPipe) < 0) {
    perror("pipe");
  }

  handlerThread = std::thread(&InterruptSignalAwaiter::waitForSignal, this);
}

void InterruptSignalAwaiter::waitForSignal() {
  // Wait for signal arrival indications.
  while (true) {
    char byte;
    int res = sys::read(signalWatchingPipe[0], &byte, 1);

    // If nothing was read, the pipe has been closed and we should shut down.
    if (res == 0) break;

    // Otherwise, check if we were awoke because of an interrupt.
    // Save and clear the interrupt flag, atomically.
    bool wasInterrupted =
        InterruptSignalAwaiter::wasInterrupted.exchange(false);

    // Process the interrupt flag, if present.
    if (wasInterrupted) {
      interruptHandler();
    }

    // Some implementations (e.g. Windows) reset the signal handler after
    // a signal has been received.
    void (*resetSignalHandler)(int) = std::signal(SIGINT, &sigintHandler);
    if (resetSignalHandler != previousSignalHandler) {
      previousSignalHandler = resetSignalHandler;
    }
  }
}

void InterruptSignalAwaiter::resetProgramSignalHandler() {
  std::signal(SIGINT, SIG_DFL);
}

InterruptSignalAwaiter::~InterruptSignalAwaiter() {
  // Deregister the signal handler.
  std::signal(SIGINT, previousSignalHandler);

  // Close the signal watching pipe.
  sys::close(signalWatchingPipe[1]);
  signalWatchingPipe[1] = -1;

  // Wait for the handler thread to finish execution
  handlerThread.join();
};
