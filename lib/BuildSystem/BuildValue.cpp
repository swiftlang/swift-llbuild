//===-- BuildValue.cpp ----------------------------------------------------===//
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

#include "llbuild/BuildSystem/BuildValue.h"

#include "llbuild/Basic/LLVM.h"

#include "llvm/Support/raw_ostream.h"

using namespace llbuild;
using namespace llbuild::buildsystem;

StringRef BuildValue::stringForKind(BuildValue::Kind kind) {
  switch (kind) {
#define CASE(kind) case Kind::kind: return #kind
    CASE(Invalid);
    CASE(VirtualInput);
    CASE(ExistingInput);
    CASE(MissingInput);
    CASE(DirectoryContents);
    CASE(DirectoryTreeSignature);
    CASE(DirectoryTreeStructureSignature);
    CASE(MissingOutput);
    CASE(FailedInput);
    CASE(SuccessfulCommand);
    CASE(FailedCommand);
    CASE(PropagatedFailureCommand);
    CASE(CancelledCommand);
    CASE(SkippedCommand);
    CASE(Target);
    CASE(StaleFileRemoval);
#undef CASE
  }
  return "<unknown>";
}
  
void BuildValue::dump(raw_ostream& os) const {
  os << "BuildValue(" << stringForKind(kind);
  if (kindHasCommandSignature()) {
    os << ", signature=" << commandSignature;
  }
  if (kindHasOutputInfo()) {
    os << ", outputInfos=[";
    for (unsigned i = 0; i != getNumOutputs(); ++i) {
      auto& info = getNthOutputInfo(i);
      if (i != 0) os << ", ";
      if (info.isMissing()) {
        os << "FileInfo{/*missing*/}";
      } else {
        os << "FileInfo{"
           << "dev=" << info.device
           << ", inode=" << info.inode
           << ", mode=" << info.mode
           << ", size=" << info.size
           << ", modTime=(" << info.modTime.seconds
           << ":" << info.modTime.nanoseconds << "}";
      }
    }
    os << "]";
  }
  if (kindHasStringList()) {
    std::vector<StringRef> values = getStringListValues();
    os << ", values=[";
    for (unsigned i = 0; i != values.size(); ++i) {
      if (i != 0) os << ", ";
      os << '"';
      os.write_escaped(values[i]);
      os << '"';
    }
    os << "]";
  }
  os << ")";
}
