//===- unittests/BuildSystem/TempDir.cpp --------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "TempDir.h"

#include "llbuild/Basic/FileSystem.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"

llbuild::TmpDir::TmpDir(llvm::StringRef namePrefix) {
    llvm::SmallString<256> tempDirPrefix;
    llvm::sys::path::system_temp_directory(true, tempDirPrefix);
    llvm::sys::path::append(tempDirPrefix, namePrefix);

    std::error_code ec = llvm::sys::fs::createUniqueDirectory(
        tempDirPrefix.str(), tempDir);
    assert(!ec);
    (void)ec;
}

llbuild::TmpDir::~TmpDir() {
    auto fs = basic::createLocalFileSystem();
    bool result = fs->remove(tempDir.c_str());
    assert(result);
    (void)result;
}

const char *llbuild::TmpDir::c_str() { return tempDir.c_str(); }
std::string llbuild::TmpDir::str() const { return tempDir.str(); }
