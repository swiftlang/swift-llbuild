//===- SignalHandlerWrappper.h ----------------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file implements a wrapper for handling signals.
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_BASIC_SIGNALHANDLERWRAPPER_H
#define LLBUILD_BASIC_SIGNALHANDLERWRAPPER_H

namespace llbuild {
namespace basic {
namespace sys {
struct SignalHandlerWrapper {
  void *innerAction;

public:
  using Handler = void (*)(int);

  SignalHandlerWrapper() {}
  SignalHandlerWrapper(Handler handler);

  void registerAction(int sig, SignalHandlerWrapper *previous);
};
}
}
}

#endif
