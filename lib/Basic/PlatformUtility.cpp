//===- Support/PlatformUtility.cpp - Platform Specific Utilities ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/PlatformUtility.h"

#if defined(_WIN32)
#include "LeanWindows.h"
#else
#include <unistd.h>
#endif

using namespace llbuild;
using namespace llbuild::basic;

bool sys::chdir(const char* fileName) {
#if defined(_WIN32)
  return SetCurrentDirectoryA(fileName);
#else
  return ::chdir(fileName) == 0;
#endif
}
