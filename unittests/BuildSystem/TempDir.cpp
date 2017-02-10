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

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"

// TODO Move this into some kind of libtestSupport?
// Cribbed from llvm, where it's been since removed.
namespace {

    using namespace std;
    using namespace llvm;
    using namespace llvm::sys::fs;

    error_code _remove_all_r(StringRef path, file_type ft, uint32_t &count) {
        if (ft == file_type::directory_file) {
            error_code ec;
            directory_iterator i(path, ec);
            if (ec)
                return ec;

            for (directory_iterator e; i != e; i.increment(ec)) {
                if (ec)
                    return ec;

                file_status st;

                if (error_code ec = i->status(st))
                    return ec;

                if (error_code ec = _remove_all_r(i->path(), st.type(), count))
                    return ec;
            }

            if (error_code ec = remove(path, false))
                return ec;

            ++count; // Include the directory itself in the items removed.
        } else {
            if (error_code ec = remove(path, false))
                return ec;

            ++count;
        }

        return error_code();
    }

    error_code remove_all(const Twine &path, uint32_t &num_removed) {
        SmallString<128> path_storage;
        StringRef p = path.toStringRef(path_storage);
        
        file_status fs;
        if (error_code ec = status(path, fs))
            return ec;
        num_removed = 0;
        return _remove_all_r(p, fs.type(), num_removed);
    }
    
    error_code remove_all(const Twine &path) {
        uint32_t num_removed = 0;
        return remove_all(path, num_removed);
    }
    
}

llbuild::TmpDir::TmpDir(llvm::StringRef namePrefix) {
    llvm::SmallString<256> tempDirPrefix;
    llvm::sys::path::system_temp_directory(true, tempDirPrefix);
    llvm::sys::path::append(tempDirPrefix, namePrefix);

    std::error_code ec = llvm::sys::fs::createUniqueDirectory
    (llvm::Twine(tempDirPrefix), tempDir);
    assert(!ec);
    (void)ec;
}

llbuild::TmpDir::~TmpDir() {
    std::error_code ec = remove_all(llvm::Twine(tempDir));
    assert(!ec);
    (void)ec;
}

const char *llbuild::TmpDir::c_str() { return tempDir.c_str(); }
std::string llbuild::TmpDir::str() const { return tempDir.str(); }
