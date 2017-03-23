//===- unittests/Commands/InterruptSignalAwaiterTests.cpp -----------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "../lib/Commands/InterruptSignalAwaiter.h"

#include "gtest/gtest.h"

#include <chrono>
#include <csignal>
#include <thread>

using namespace llbuild;
using namespace llbuild::command;

namespace {

/// Check that we call the interrupt handler when raising a SIGINT signal.
TEST(InterruptSignalAwaiterTests, setInterruptHandler) {
  bool calledInterruptHandler = false;
  InterruptSignalAwaiter::GlobalAwaiter.setInterruptHandler(
      [&] { calledInterruptHandler = true; });

  // Raise a signal and wait a bit to make sure the interrupt signal
  // handler was called.
  std::raise(SIGINT);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_TRUE(calledInterruptHandler);
}

/// Check that we don't call the interrupt handler when reset.
TEST(InterruptSignalAwaiterTests, resetInterruptHandler) {
  bool calledInterruptHandler = false;
  InterruptSignalAwaiter::GlobalAwaiter.setInterruptHandler(
      [&] { calledInterruptHandler = true; });

  InterruptSignalAwaiter::GlobalAwaiter.resetInterruptHandler();

  // Raise a signal and wait a bit to make sure the interrupt signal
  // handler was called.
  std::raise(SIGINT);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  EXPECT_FALSE(calledInterruptHandler);
}
}
