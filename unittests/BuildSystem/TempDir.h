//===- unittests/BuildSystem/TempDir.h --------------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#ifndef LLBUILD_TESTS_TEMPDIR
#define LLBUILD_TESTS_TEMPDIR

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

namespace llbuild {

// TODO Move this into some kind of libtestSupport?
/// Creates a temporary directory in its constructor and removes it in its
/// destructor. Makes it available via str() and c_str().
class TmpDir {
private:
    TmpDir(const TmpDir&) = delete;
    TmpDir& operator=(const TmpDir&) = delete;

    llvm::SmallString<256> tempDir;

public:
    TmpDir(llvm::StringRef namePrefix = "");
    ~TmpDir();

    const char *c_str();
    std::string str() const;
};

}

#endif /* LLBUILD_TESTS_TEMPDIR */
