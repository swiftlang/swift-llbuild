//===- BuildSystemCommandInterface.h ----------------------------*- C++ -*-===//
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

#ifndef LLBUILD_BUILDSYSTEM_BUILDSYSTEMCOMMANDINTERFACE_H
#define LLBUILD_BUILDSYSTEM_BUILDSYSTEMCOMMANDINTERFACE_H

// FIXME: Eliminate need for this include, if we could forward declare the value
// type.
#include "llbuild/Core/BuildEngine.h"

namespace llbuild {
namespace buildsystem {

/// This is an abstract interface class which defines the API available to
/// Commands when being invoked by the BuildSystem for the purposes of
/// execution.
//
// FIXME: This could avoid using virtual dispatch.
class BuildSystemCommandInterface {
public:
  virtual ~BuildSystemCommandInterface();

  virtual void taskIsComplete(core::Task* task, core::ValueType&& value,
                              bool forceChange = false) = 0;
};

}
}

#endif
