//===- Support/PlatformUtility.cpp - Platform Specific Utilities ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llbuild/Basic/PlatformUtility.h"
#include "llbuild/Basic/Stat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ConvertUTF.h"

#if defined(_WIN32)
#include "LeanWindows.h"
#include <Shlwapi.h>
#include <direct.h>
#include <io.h>
#else
#include <fnmatch.h>
#include <unistd.h>
#endif
#include <stdio.h>

using namespace llbuild;
using namespace llbuild::basic;

bool sys::chdir(const char *fileName) {
#if defined(_WIN32)
  return SetCurrentDirectoryA(fileName);
#else
  return ::chdir(fileName) == 0;
#endif
}

int sys::close(int fileHandle) {
#if defined(_WIN32)
  return ::_close(fileHandle);
#else
  return ::close(fileHandle);
#endif
}

int sys::lstat(const char *fileName, sys::StatStruct *buf) {
#if defined(_WIN32)
  // We deliberately ignore lstat on Windows, and delegate
  // to stat.
  return ::_stat(fileName, buf);
#else
  return ::lstat(fileName, buf);
#endif
}

bool sys::mkdir(const char* fileName) {
#if defined(_WIN32)
  return _mkdir(fileName) == 0;
#else
  return ::mkdir(fileName, S_IRWXU | S_IRWXG |  S_IRWXO) == 0;
#endif
}

int sys::pclose(FILE *stream) {
#if defined(_WIN32)
  return ::_pclose(stream);
#else
  return ::pclose(stream);
#endif
}

int sys::pipe(int ptHandles[2]) {
#if defined(_WIN32)
  return ::_pipe(ptHandles, 0, 0);
#else
  return ::pipe(ptHandles);
#endif
}

FILE *sys::popen(const char *command, const char *mode) {
#if defined(_WIN32)
  return ::_popen(command, mode);
#else
  return ::popen(command, mode);
#endif
}

int sys::read(int fileHandle, void *destinationBuffer,
  unsigned int maxCharCount) {
#if defined(_WIN32)
  return ::_read(fileHandle, destinationBuffer, maxCharCount);
#else
  return ::read(fileHandle, destinationBuffer, maxCharCount);
#endif
}

int sys::rmdir(const char *path) {
#if defined(_WIN32)
  return ::_rmdir(path);
#else
  return ::rmdir(path);
#endif
}

int sys::stat(const char *fileName, StatStruct *buf) {
#if defined(_WIN32)
  return ::_stat(fileName, buf);
#else
  return ::stat(fileName, buf);
#endif
}

int sys::symlink(const char *source, const char *target) {
#if defined(_WIN32)
  DWORD attributes = GetFileAttributesA(source);
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return ::CreateSymbolicLinkA(source, target, SYMBOLIC_LINK_FLAG_DIRECTORY);
  }

  return ::CreateSymbolicLinkA(source, target, 0);
#else
  return ::symlink(source, target);
#endif
}

int sys::unlink(const char *fileName) {
#if defined(_WIN32)
  return ::_unlink(fileName);
#else
  return ::unlink(fileName);
#endif
}

int sys::write(int fileHandle, void *destinationBuffer,
  unsigned int maxCharCount) {
#if defined(_WIN32)
  return ::_write(fileHandle, destinationBuffer, maxCharCount);
#else
  return ::write(fileHandle, destinationBuffer, maxCharCount);
#endif
}

// Raise the open file limit, returns 0 on success, -1 on failure
int sys::raiseOpenFileLimit(llbuild_rlim_t limit) {
#if defined(_WIN32)
  int curLimit = _getmaxstdio();
  if (curLimit >= limit) {
    return 0;
  }
  // 2048 is the hard upper limit on Windows
  return _setmaxstdio(std::min(limit, 2048)) == -1 ? -1 : 0;
#else
  int ret = 0;

  struct rlimit rl;
  ret = getrlimit(RLIMIT_NOFILE, &rl);
  if (ret != 0) {
    return ret;
  }

  if (rl.rlim_cur >= limit) {
    return 0;
  }

  rl.rlim_cur = std::min(limit, rl.rlim_max);

  return setrlimit(RLIMIT_NOFILE, &rl);
#endif
}

sys::MATCH_RESULT sys::filenameMatch(const std::string& pattern,
                                     const std::string& filename) {
#if defined(_WIN32)
  llvm::SmallVector<UTF16, 20> wpattern;
  llvm::SmallVector<UTF16, 20> wfilename;

  llvm::convertUTF8ToUTF16String(pattern, wpattern);
  llvm::convertUTF8ToUTF16String(filename, wfilename);

  bool result =
      PathMatchSpecW((LPCWSTR)wfilename.data(), (LPCWSTR)wpattern.data());
  return result ? sys::MATCH : sys::NO_MATCH;
#else
  int result = fnmatch(pattern.c_str(), filename.c_str(), 0);
  return result == 0 ? sys::MATCH
                     : result == FNM_NOMATCH ? sys::NO_MATCH : sys::MATCH_ERROR;
#endif
}

void sys::sleep(int seconds) {
#if defined(_WIN32)
  // Uses milliseconds
  Sleep(seconds * 1000);
#else
  ::sleep(seconds);
#endif
}

std::string sys::strerror(int error) {
#if defined(_WIN32)
  LPWSTR errBuff;
  int count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, error, 0, (LPWSTR)&errBuff, 0, nullptr);
  llvm::ArrayRef<wchar_t> wRef(errBuff, errBuff + count);
  llvm::ArrayRef<char> uRef(reinterpret_cast<const char *>(wRef.begin()),
                            reinterpret_cast<const char *>(wRef.end()));
  std::string utf8Err;
  llvm::convertUTF16ToUTF8String(llvm::ArrayRef<char>(uRef), utf8Err);
  LocalFree(errBuff);
  return utf8Err;
#else
  return ::strerror(error);
#endif
}

char *sys::strsep(char **stringp, const char *delim) {
#if defined(_WIN32)
  char *begin, *end = *stringp;
  if (*stringp == NULL) {
    return NULL;
  }
  int delimLen = strlen(delim);
  bool found = false;
  while (*end) {
    for (int i = 0; i < delimLen; i++) {
      if (*end == delim[i]) {
        found = true;
      }
    }
    if (found) {
      *stringp = end + 1;
    }
  }
  *end = '\0';
  return begin;
#else
  return ::strsep(stringp, delim);
#endif
}

std::string sys::makeTmpDir() {
#if defined(_WIN32)
  char path[MAX_PATH];
  tmpnam_s(path, MAX_PATH);
  return std::string(path);
#else
  char tmpDirPathBuf[] = "/tmp/fileXXXXXX";
  return std::string(mkdtemp(tmpDirPathBuf));
#endif
}
