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
#include "llvm/Support/Path.h"

#if defined(_WIN32)
#include "LeanWindows.h"
#include <Shlwapi.h>
#include <direct.h>
#include <io.h>
#include <time.h>
#else
#include <fnmatch.h>
#include <unistd.h>
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <dlfcn.h>
#endif
#endif
#include <stdio.h>

#if defined(_WIN32)
const HANDLE llbuild::basic::sys::FileDescriptorTraits<HANDLE>::InvalidDescriptor =
    INVALID_HANDLE_VALUE;
#endif

using namespace llbuild;
using namespace llbuild::basic;

bool sys::chdir(const char *fileName) {
#if defined(_WIN32)
  llvm::SmallVector<wchar_t, MAX_PATH> wFileName;
  if (llvm::sys::path::widenPath(fileName, wFileName))
    return false;
  return ::_wchdir(wFileName.data()) == 0;
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

#if defined(_WIN32)
time_t filetimeToTime_t(FILETIME ft) {
  long long ltime = ft.dwLowDateTime | ((long long)ft.dwHighDateTime << 32);
  return (time_t)((ltime - 116444736000000000) / 10000000);
}
#endif

int sys::lstat(const char *fileName, sys::StatStruct *buf) {
#if defined(_WIN32)
  llvm::SmallVector<wchar_t, MAX_PATH> wfilename;
  if (llvm::sys::path::widenPath(fileName, wfilename)) {
    errno = EINVAL;
    return -1;
  }
  HANDLE h = CreateFileW(
      /*lpFileName=*/wfilename.data(),
      /*dwDesiredAccess=*/0,
      /*dwShareMode=*/FILE_SHARE_READ,
      /*lpSecurityAttributes=*/NULL,
      /*dwCreationDisposition=*/OPEN_EXISTING,
      /*dwFlagsAndAttributes=*/FILE_FLAG_OPEN_REPARSE_POINT |
          FILE_FLAG_BACKUP_SEMANTICS,
      /*hTemplateFile=*/NULL);
  if (h == INVALID_HANDLE_VALUE) {
    int err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND) {
      errno = ENOENT;
    }
    return -1;
  }
  BY_HANDLE_FILE_INFORMATION info;
  GetFileInformationByHandle(h, &info);
  // Group id is always 0 on Windows
  buf->st_gid = 0;
  buf->st_atime = filetimeToTime_t(info.ftLastAccessTime);
  buf->st_ctime = filetimeToTime_t(info.ftCreationTime);
  buf->st_dev = info.dwVolumeSerialNumber;
  // inodes have meaning on FAT/HPFS/NTFS
  buf->st_ino = 0;
  buf->st_rdev = info.dwVolumeSerialNumber;
  buf->st_mode =
      // On a symlink to a directory, Windows sets both the REPARSE_POINT and
      // DIRECTORY attributes. Since Windows doesn't provide S_IFLNK and we
      // want unix style "symlinks to directories are not directories
      // themselves, we say symlinks are regular files
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
          ? _S_IFREG
          : (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? _S_IFDIR
                                                               : _S_IFREG;
  buf->st_mode |= (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                      ? _S_IREAD
                      : _S_IREAD | _S_IWRITE;
  llvm::StringRef extension =
      llvm::sys::path::extension(llvm::StringRef(fileName));
  if (extension == ".exe" || extension == ".cmd" || extension == ".bat" ||
      extension == ".com") {
    buf->st_mode |= _S_IEXEC;
  }
  buf->st_mtime = filetimeToTime_t(info.ftLastWriteTime);
  buf->st_nlink = info.nNumberOfLinks;
  buf->st_size = ((long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
  // Uid is always 0 on Windows systems
  buf->st_uid = 0;
  CloseHandle(h);
  return 0;
#else
  return ::lstat(fileName, buf);
#endif
}

bool sys::mkdir(const char* fileName) {
#if defined(_WIN32)
  llvm::SmallVector<wchar_t, MAX_PATH> wfilename;
  if (llvm::sys::path::widenPath(fileName, wfilename))
    return false;
  return ::_wmkdir(wfilename.data()) == 0;
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
  llvm::SmallVector<wchar_t, MAX_PATH> wpath;
  if (llvm::sys::path::widenPath(path, wpath)) {
    errno = EINVAL;
    return -1;
  }
  return ::_wrmdir(wpath.data());
#else
  return ::rmdir(path);
#endif
}

int sys::stat(const char *fileName, StatStruct *buf) {
#if defined(_WIN32)
  llvm::SmallVector<wchar_t, MAX_PATH> wfilename;
  if (llvm::sys::path::widenPath(fileName, wfilename)) {
    errno = EINVAL;
    return -1;
  }
  return ::_wstat(wfilename.data(), buf);
#else
  return ::stat(fileName, buf);
#endif
}

// Create a symlink named linkPath which contains the string pointsTo
int sys::symlink(const char *pointsTo, const char *linkPath) {
#if defined(_WIN32)
  llvm::SmallVector<wchar_t, MAX_PATH> wPointsTo;
  if (llvm::sys::path::widenPath(pointsTo, wPointsTo))
    return -1;
  llvm::SmallVector<wchar_t, MAX_PATH> wLinkPath;
  if (llvm::sys::path::widenPath(linkPath, wLinkPath))
    return -1;
  DWORD attributes = GetFileAttributesW(wPointsTo.data());
  DWORD directoryFlag = (attributes != INVALID_FILE_ATTRIBUTES &&
                         attributes & FILE_ATTRIBUTE_DIRECTORY)
                            ? SYMBOLIC_LINK_FLAG_DIRECTORY
                            : 0;
  // Note that CreateSymbolicLinkW takes its arguments in reverse order
  // compared to symlink/_symlink
  return !::CreateSymbolicLinkW(
      wLinkPath.data(), wPointsTo.data(),
      SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | directoryFlag);
#else
  return ::symlink(pointsTo, linkPath);
#endif
}

int sys::unlink(const char *fileName) {
#if defined(_WIN32)
  llvm::SmallVector<wchar_t, MAX_PATH> wfilename;
  if (llvm::sys::path::widenPath(fileName, wfilename)) {
    errno = EINVAL;
    return -1;
  }
  return ::_wunlink(wfilename.data());
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

// Get the current process' open file limit. Returns -1 on failure.
llbuild_rlim_t sys::getOpenFileLimit() {
#if defined(_WIN32)
  return _getmaxstdio();
#else
  struct rlimit rl;
  int ret = getrlimit(RLIMIT_NOFILE, &rl);
  if (ret != 0) {
    return 0;
  }

  return rl.rlim_cur;
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
  llvm::SmallVector<wchar_t, MAX_PATH> wpattern;
  llvm::SmallVector<wchar_t, MAX_PATH> wfilename;

  if (llvm::sys::path::widenPath(pattern, wpattern) ||
      llvm::sys::path::widenPath(filename, wfilename))
    return sys::MATCH_ERROR;

  bool result =
      PathMatchSpecW(wfilename.data(), wpattern.data());
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
  // If *stringp is NULL, the strsep() function returns NULL and does nothing
  // else.
  if (*stringp == NULL) {
    return NULL;
  }
  char *begin = *stringp;
  char *end = *stringp;
  do {
    // Otherwise, this function finds the first token in the string *stringp,
    // that is delimited by one of the bytes in the string delim.
    for (int i = 0; delim[i] != '\0'; i++) {
      if (*end == delim[i]) {
        // This token is terminated by overwriting the delimiter with a null
        // byte ('\0'), and *stringp is updated to point past the token.
        *end = '\0';
        *stringp = end + 1;
        return begin;
      }
    }
  } while (*(++end));
  // In case no delimiter was found, the token is taken to be the entire string
  // *stringp, and *stringp is made NULL.
  *stringp = NULL;
  return begin;
#else
  return ::strsep(stringp, delim);
#endif
}

std::string sys::makeTmpDir() {
#if defined(_WIN32)
  char path[MAX_PATH];
  tmpnam_s(path, MAX_PATH);
  llvm::SmallVector<wchar_t, MAX_PATH> wPath;
  if (llvm::sys::path::widenPath(path, wPath))
    return std::string();
  if (!CreateDirectoryW(wPath.data(), NULL)) {
    DWORD error = GetLastError();
    if (error != ERROR_ALREADY_EXISTS) {
      fprintf(stderr, "Failed to create temporary directory '%s': error code %lu\n",
              path, (unsigned long)error);
      return std::string();
    }
  } 
  return std::string(path);
#else
  if (const char *tmpDir = std::getenv("TMPDIR")) {
    char *tmpDirPath = nullptr;
    asprintf(&tmpDirPath, "%s/fileXXXXXX", tmpDir);
    auto tmpDirString = std::string(mkdtemp(tmpDirPath));
    free(tmpDirPath);
    return tmpDirString;
  }
  else {
    char tmpDirPathBuf[] = "/tmp/fileXXXXXX";
    return std::string(mkdtemp(tmpDirPathBuf));
  }
#endif
}

std::string sys::getPathSeparators() {
#if defined(_WIN32)
  return "/\\";
#else
  return "/";
#endif
}

sys::ModuleTraits<>::Handle sys::OpenLibrary(const char *path) {
#if defined(_WIN32)
  llvm::SmallVector<wchar_t, MAX_PATH> wPath;
  if (llvm::sys::path::widenPath(path, wPath))
    return nullptr;
  return LoadLibraryW(wPath.data());
#else
  return dlopen(path, RTLD_LAZY);
#endif
}

void *sys::GetSymbolByname(sys::ModuleTraits<>::Handle handle,
                           const char *name) {
#if defined(_WIN32)
  return GetProcAddress(handle, name);
#else
  return dlsym(handle, name);
#endif
}

void sys::CloseLibrary(sys::ModuleTraits<>::Handle handle) {
#if defined(_WIN32)
  FreeLibrary(handle);
#else
  dlclose(handle);
#endif
}

