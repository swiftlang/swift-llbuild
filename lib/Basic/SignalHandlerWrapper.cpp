//===- Support/SignalHandlerWrapper.cpp - Signal handling -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/SignalHandlerWrapper.h"

#include <signal.h>

using namespace llbuild;
using namespace llbuild::basic;
using namespace llbuild::basic::sys;

SignalHandlerWrapper::SignalHandlerWrapper(Handler handler) {
#if defined(_WIN32)
  innerAction = handler;
#else
  struct sigaction action{};
  action.sa_handler = handler;
  innerAction = handler;
#endif
}

void SignalHandlerWrapper::registerAction(int sig,
                                          SignalHandlerWrapper *previous) {
#if defined(_WIN32)
  Handler handler = ::signal(sig, static_cast<Handler>(innerAction));
  if (previous) {
    previous->innerAction = handler;
  }
#else
  struct sigaction action = static_cast<struct sigaction>(innerAction);

  struct sigaction previousHandler;
  ::sigaction(sig, &action, &previousHandler);
  if (previous) {
    previous->innerAction = previousHandler;
  }
#endif
}
