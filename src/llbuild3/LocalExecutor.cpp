//===-- LocalExecutor.cpp -------------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2018 - 2025 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild3/LocalExecutor.h"

#include <llbuild3/Errors.hpp>
#include <llbuild3/Result.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#if !defined(_WIN32)
#include <poll.h>
#endif

#if defined(_WIN32)
#include <process.h>
#include <psapi.h>
#include <windows.h>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Compiler.h"

#else
#include <spawn.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <pthread/spawn.h>
#include "TargetConditionals.h"
#if !TARGET_OS_IPHONE
extern "C" {
  // Provided by System.framework's libsystem_kernel interface
  extern int __pthread_chdir(const char *path);
  extern int __pthread_fchdir(int fd);
}

/// Set the thread specific working directory to the given path.
int pthread_chdir_np(const char *path)
{
  return __pthread_chdir(path);
}

/// Set the thread specific working directory to that of the given file
/// descriptor. Passing -1 clears the thread specific working directory,
/// returning it to the process level working directory.
int pthread_fchdir_np(int fd)
{
  return __pthread_fchdir(fd);
}
#endif
#endif

/* Define to 1 if you have the `posix_spawn' function. */
#if !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define HAVE_POSIX_SPAWN 1
#endif

#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(maj, min) 0
#endif

#if !defined(_WIN32) && defined(HAVE_POSIX_SPAWN)
// Implementation mostly copied from _CFPosixSpawnFileActionsChdir in swift-corelibs-foundation
static int posix_spawn_file_actions_addchdir_polyfill(posix_spawn_file_actions_t * __restrict file_actions,
                                                      const char * __restrict path) {
#if defined(__GLIBC__) && !__GLIBC_PREREQ(2, 29)
  // Glibc versions prior to 2.29 don't support posix_spawn_file_actions_addchdir_np, impacting:
  //  - Amazon Linux 2 (EoL mid-2025)
  return ENOSYS;
#elif defined(__ANDROID__) && __ANDROID_API__ < 34
  // Android versions prior to 14 (API level 34) don't support posix_spawn_file_actions_addchdir_np
  return ENOSYS;
#elif defined(__OpenBSD__) || defined(__QNX__)
  // Currently missing as of:
  //  - OpenBSD 7.5 (April 2024)
  //  - QNX 8 (December 2023)
  return ENOSYS;
#elif defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED < 101500
  // Conditionally available on macOS if building with a deployment target older than 10.15
  if (__builtin_available(macOS 10.15, *)) {
    return posix_spawn_file_actions_addchdir_np(file_actions, path);
  }
  return ENOSYS;
#elif defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__ANDROID__) || defined(__musl__)
  // Pre-standard posix_spawn_file_actions_addchdir_np version available in:
  //  - Solaris 11.3 (October 2015)
  //  - Glibc 2.29 (February 2019)
  //  - macOS 10.15 (October 2019)
  //  - musl 1.1.24 (October 2019)
  //  - FreeBSD 13.1 (May 2022)
  //  - Android 14 (October 2023)
  return posix_spawn_file_actions_addchdir_np((posix_spawn_file_actions_t *)file_actions, path);
#else
  // Standardized posix_spawn_file_actions_addchdir version (POSIX.1-2024, June 2024) available in:
  //  - Solaris 11.4 (August 2018)
  //  - NetBSD 10.0 (March 2024)
  return posix_spawn_file_actions_addchdir((posix_spawn_file_actions_t *)file_actions, path);
#endif
}
#endif

using namespace llbuild3;

namespace {
inline Error makeLocalExecutorError(ExecutorError code,
                             std::string desc = std::string()) {
  Error err;
  err.set_type(ErrorType::EXECUTOR);
  err.set_code(rawCode(code));
  if (!desc.empty()) {
    err.set_description(desc);
  }
  return err;
}
}

namespace llbuild3 {
namespace sys {

int pipe(int ptHandles[2]) {
#if defined(_WIN32)
  return ::_pipe(ptHandles, 0, 0);
#else
  return ::pipe(ptHandles);
#endif
}

std::string strerror(int error) {
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

#if defined(_WIN32)
std::string formatWindowsCommandArg(StringRef string) {
  // Windows escaping, adapted from Daniel Colascione's "Everyone quotes
  // command line arguments the wrong way" - Microsoft Developer Blog
  const std::string needsQuote = " \t\n\v\"";
  if (string.find_first_of(needsQuote) == std::string::npos) {
    return string;
  }

  // To escape the command line, we surround the argument with quotes. However
  // the complication comes due to how the Windows command line parser treats
  // backslashes (\) and quotes (")
  //
  // - \ is normally treated as a literal backslash
  //     - e.g. foo\bar\baz => foo\bar\baz
  // - However, the sequence \" is treated as a literal "
  //     - e.g. foo\"bar => foo"bar
  //
  // But then what if we are given a path that ends with a \? Surrounding
  // foo\bar\ with " would be "foo\bar\" which would be an unterminated string
  // since it ends on a literal quote. To allow this case the parser treats:
  //
  // - \\" as \ followed by the " metachar
  // - \\\" as \ followed by a literal "
  // - In general:
  //     - 2n \ followed by " => n \ followed by the " metachar
  //     - 2n+1 \ followed by " => n \ followed by a literal "
  std::string escaped = "\"";
  for (auto i = std::begin(string); i != std::end(string); ++i) {
    int numBackslashes = 0;
    while (i != string.end() && *i == '\\') {
      ++i;
      ++numBackslashes;
    }

    if (i == string.end()) {
      // String ends with a backslash e.g. foo\bar\, escape all the backslashes
      // then add the metachar " below
      escaped.append(numBackslashes * 2, '\\');
      break;
    } else if (*i == '"') {
      // This is  a string of \ followed by a " e.g. foo\"bar. Escape the
      // backslashes and the quote
      escaped.append(numBackslashes * 2 + 1, '\\');
      escaped.push_back(*i);
    } else {
      // These are just literal backslashes
      escaped.append(numBackslashes, '\\');
      escaped.push_back(*i);
    }
  }
  escaped.push_back('"');

  return escaped;
}

std::string formatWindowsCommandString(std::vector<std::string> args) {
    std::string commandLine;
    for (auto& arg : args)
        commandLine += formatWindowsCommandArg(arg) + " ";
    if (commandLine.size())
      commandLine.pop_back();
    return commandLine;
}
#endif


std::error_code checkExecutable(const std::filesystem::path& path) {

#if defined(_WIN32)
  llvm::SmallVector<wchar_t, 128> wpath;
  if (llvm::sys::path::widenPath(path.c_str(), wpath)) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  if (::_waccess(path.c_str(), R_OK | X_OK) == -1) {
    return std::error_code(errno, std::generic_category());
  }
#else
  if (::access(path.c_str(), R_OK | X_OK) == -1) {
    return std::error_code(errno, std::generic_category());
  }
#endif

  // Don't say that directories are executable.
#if defined(_WIN32)
  struct ::_stat buf;
#else
  struct ::stat buf;
#endif

#if defined(_WIN32)
  if (0 != ::_wstat(wpath.data(), &buf)) {
#else
  if (0 != ::stat(path.c_str(), &buf)) {
#endif
    return std::make_error_code(std::errc::permission_denied);
  }

  if (!S_ISREG(buf.st_mode))
    return std::make_error_code(std::errc::permission_denied);

  return std::error_code();
}

result<std::string, Error> findProgramByName(std::string_view name) {
  assert(!name.empty() && "must have a name!");

  // Use the given path verbatim if it contains any slashes; this matches
  // the behavior of sh(1) and friends.
  if (name.find('/') != std::string_view::npos) {
    return std::string(name);
  }

  const char *envPath = std::getenv("PATH");
  if (!envPath) {
    return fail(makeLocalExecutorError(ExecutorError::FileNotFound));
  }
  for (const auto path : std::views::split(std::string_view(envPath), ":")) {
    if (path.empty())
      continue;

    // Check to see if this first directory contains the executable...
    const auto svpath = std::string_view(path.begin(), path.end());
    auto filepath = std::filesystem::path(svpath);
    filepath.append(name);

    std::error_code ec = checkExecutable(filepath);
    if (!ec)
      return std::string(filepath);
  }
  return fail(makeLocalExecutorError(ExecutorError::FileNotFound));
}

}
}

namespace {

std::pair<std::string_view, std::string_view> split(std::string_view str, std::string_view delim) {
  auto pos = str.find(delim);
  if (pos == std::string_view::npos) {
    return std::make_pair(str, "");
  }
  auto begin = str.substr(0, pos);
  str.remove_prefix(pos + delim.size());
  return std::make_pair(begin, str);
}

/// A helper class for constructing a POSIX-style environment.
class POSIXEnvironment {
  /// The actual environment, this is only populated once frozen.
#if defined(_WIN32)
  std::vector<wchar_t> env;
#else
  std::vector<const char*> env;
#endif

  /// The underlying string storage.
  //
  // FIXME: This is not efficient, we could store into a single allocation.
  std::vector<std::string> envStorage;

  /// The list of known keys in the environment.
  std::unordered_set<std::string_view> keys{};

  /// Whether the environment pointer has been vended, and assignments can no
  /// longer be mutated.
  bool isFrozen = false;

public:
  POSIXEnvironment() {}

  /// Add a key to the environment, if missing.
  ///
  /// If the key has already been defined, it will **NOT** be inserted.
  void setIfMissing(std::string_view key, std::string_view value) {
    assert(!isFrozen);
    if (keys.insert(key).second) {
      std::string assignment;
      assignment += key;
      assignment += '=';
      assignment += value;
      assignment += '\0';
      envStorage.emplace_back(assignment);
    }
  }

#if defined(_WIN32)
  /// Get a Windows style environment pointer.
  ///
  /// This pointer is only valid for the lifetime of the environment itself.
  /// CreateProcessW requires a mutable pointer, so we allocate and return a
  /// copy.
  std::unique_ptr<wchar_t[]> getWindowsEnvp() {
    isFrozen = true;

    // Form the final environment.
    // On Windows, the environment must be a contiguous null-terminated block
    // of null-terminated strings followed by an additional null terminator
    env.clear();
    for (const auto& entry : envStorage) {
      llvm::SmallVector<llvm::UTF16, 20> wEntry;
      llvm::convertUTF8ToUTF16String(entry, wEntry);
      env.insert(env.end(), wEntry.begin(), wEntry.end());
    }
    env.emplace_back(L'\0');
    auto envData = std::make_unique<wchar_t[]>(env.size());
    std::copy(env.begin(), env.end(), envData.get());
    return envData;
  };
#else
  /// Get a POSIX style envirnonment pointer.
  ///
  /// This pointer is only valid for the lifetime of the environment itself.
  const char* const* getEnvp() {
    isFrozen = true;

    // Form the final environment.
    env.clear();
    for (const auto& entry : envStorage) {
      env.emplace_back(entry.c_str());
    }
    env.emplace_back(nullptr);
    return env.data();
  }
#endif
};


// MARK: Process Group

class ProcessGroup {
  ProcessGroup(const ProcessGroup&) = delete;
  void operator=(const ProcessGroup&) = delete;
  ProcessGroup& operator=(ProcessGroup&&) = delete;

  std::unordered_map<llbuild_pid_t, ProcessInfo> processes;
  std::condition_variable processesCondition;
  bool closed = false;

public:
  ProcessGroup() {}
  ~ProcessGroup() {
    // Wait for all processes in the process group to terminate
    std::unique_lock<std::mutex> lock(mutex);
    while (!processes.empty()) {
      processesCondition.wait(lock);
    }
  }

  std::mutex mutex;

  void close() { closed = true; }
  bool isClosed() const { return closed; }

  void add(std::lock_guard<std::mutex>&& lock, llbuild_pid_t pid,
           ProcessInfo info) {
    processes.emplace(std::make_pair(pid, info));
  }

  void remove(llbuild_pid_t pid) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      processes.erase(pid);
    }
    processesCondition.notify_all();
  }

  void signalAll(int signal) {
    std::lock_guard<std::mutex> lock(mutex);

    for (const auto& it: processes) {
      // If we are interrupting, only interupt processes which are believed to
      // be safe to interrupt.
      if (signal == SIGINT && !it.second.canSafelyInterrupt)
        continue;

      // We are killing the whole process group here, this depends on us
      // spawning each process in its own group earlier.
  #if defined(_WIN32)
      TerminateProcess(it.first, signal);
  #else
      ::kill(-it.first, signal);
  #endif
    }
  }
};

#if defined(_WIN32)
template <> struct FileDescriptorTraits<HANDLE> {
  typedef HANDLE DescriptorType;
  static const HANDLE InvalidDescriptor;
  static bool IsValid(HANDLE hFile) { return hFile != InvalidDescriptor; }
  static void Close(HANDLE hFile) { CloseHandle(hFile); }
  static int Read(HANDLE hFile, void *destinationBuffer,
                  unsigned int maxCharCount) {
    DWORD numBytes;
    if (!ReadFile(hFile, destinationBuffer, maxCharCount, &numBytes, nullptr)) {
      return -1;
    }
    return numBytes;
  }
};
#endif

template <typename = FD> struct FileDescriptorTraits;

template <> struct FileDescriptorTraits<int> {
  typedef int DescriptorType;
  static const int InvalidDescriptor = -1;
  static bool IsValid(int fd) { return fd >= 0; }
  static void Close(int fd) { close(fd); }
  static size_t Read(int hFile, void *destinationBuffer,
                  unsigned int maxCharCount) {
    return read(hFile, destinationBuffer, maxCharCount);
  }
};


/// Remember to automatically close the descriptor when it goes out of scope.
/// This helps to keep the file descriptor alive until forwarded to the process.
/// After that we don't need to keep it around.
class ManagedDescriptor {
public:

  /// A short-hand type for a platform-independent descriptor.
  using FileDescriptor = FileDescriptorTraits<>::DescriptorType;

private:

  /// Open the trait namespace to shorten the code.
  using fdTraits = FileDescriptorTraits<>;

  /// Underlying file descriptor.
  FileDescriptor _descriptor = fdTraits::InvalidDescriptor;

#ifndef  NDEBUG
  /// Whether the descriptor has been properly closed.
  bool _closedProperly = true;

  /// File and line number this descriptor was allocated at.
  const char *_file = nullptr;
  unsigned _line = 0;
#endif

public:

  /// Create the descriptor which doesn't describe anything.
  ManagedDescriptor() : _descriptor(fdTraits::InvalidDescriptor) { }

  /// Store and retrieve the source code information.
  /// Useful for tracking leaking descriptors.
#ifndef NDEBUG
  ManagedDescriptor(const char *file, unsigned line)
    : _file(file), _line(line) { (void)_file; (void)_line; }
  const char *file() { return _file; }
  unsigned line() { return _line; }
#else
  ManagedDescriptor(const char *file, unsigned line) { }
  const char *file() { return __FILE__; }
  unsigned line() { return 0; }
#endif

  /// Create the descriptor which autocloses if it goes out of scope.
  ManagedDescriptor(FileDescriptor &fd) { reset(fd); }

  /// Must not ever copy to avoid double-closure.
  ManagedDescriptor(const ManagedDescriptor &) = delete;

  /// Can move descriptors just fine.
  ManagedDescriptor(ManagedDescriptor&& other) {
    _descriptor = other._descriptor;
    other._descriptor = fdTraits::InvalidDescriptor;
#ifndef NDEBUG
    assert(_closedProperly);
    _closedProperly = isValid() ? false : other._closedProperly;
    other._closedProperly = true;
    _file = other._file;
    _line = other._line;
#endif
  }

  /// Close the file descriptor.
  /// Safely autocloses in release mode.
  /// Asserts if the descriptors was about to be autoclosed.
  ~ManagedDescriptor() {
    assert(!isValid() && _closedProperly);
    close();
  }

  /// Copy the underlying descriptor out.
  FileDescriptor unsafeDescriptor() const {
    return _descriptor;
  }

  /// Whether descriptor has been initialized to a valid value and not closed.
  bool isValid() const {
    return fdTraits::IsValid(_descriptor);
  }

  /// Replace the existing descriptor with a given one,
  /// invalidating the passed descriptor.
  ManagedDescriptor &reset(FileDescriptor &fd) {
    close();
    _descriptor = fd;
#ifndef NDEBUG
    _closedProperly = false;
#endif
    fd = fdTraits::InvalidDescriptor;
    return *this;
  }

  /// Set inheritability of a given file descriptor.
  /// true  - Ensure the descriptor is inherited by the child process.
  /// false - Prevent leaking the descriptor into a child process.
  ManagedDescriptor &childMayInherit(bool yes) {
    if (isValid()) {
      return *this;
    }

    auto fd = _descriptor;

#if defined(_WIN32)
    SetHandleInformation(fd, HANDLE_FLAG_INHERIT, yes ? TRUE : FALSE);
#else
    if (yes) {
      fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
    } else {
      fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) & ~FD_CLOEXEC);
    }
#endif
    return *this;
  }

  /// Explicitly close the descriptor.
  bool close() {
    if (!isValid()) {
      return false;
    }

    auto fd = _descriptor;
    _descriptor = fdTraits::InvalidDescriptor;
#ifndef NDEBUG
    _closedProperly = true;
#endif
    fdTraits::Close(fd);
    return true;
  }
};

// Manage the state of a control protocol channel
//
// FIXME: This really should move out of subprocess and up a layer or two. The
// process code should primarily handle reading file descriptors and pushing the
// data up. For now, though, the goal is to move the code out of build system
// and into a reusable layer.
class ControlProtocolState {
  std::string controlID;

  bool negotiated = false;
  std::string partialMsg;
  bool releaseSeen = false;

  const size_t maxLength = 16;

public:
  ControlProtocolState(const std::string& controlID) : controlID(controlID) {}

  /// Reads incoming control message buffer
  ///
  /// \return 0 on success, 1 on completion, -1 on error.
  size_t read(std::string_view buf, std::string* errstr = nullptr) {
    while (buf.size()) {
      size_t nl = buf.find('\n');
      if (nl == std::string::npos) {
        if (partialMsg.size() + buf.size() > maxLength) {
          // protocol fault, msg length exceeded maximum
          partialMsg.clear();
          if (errstr) {
            *errstr = "excessive message length";
          }
          return -1;
        }

        // incomplete msg, store and continue
        partialMsg += buf;
        return 0;
      }

      partialMsg += buf.substr(0, nl);

      if (!negotiated) {
        // negotiate protocol version
        if (partialMsg != "llbuild.1") {
          // incompatible protocol version
          if (errstr) {
            *errstr = "unsupported protocol: " + partialMsg;
          }
          partialMsg.clear();
          return -1;
        }
        negotiated = true;
      } else {
        // check for supported control message
        if (partialMsg == controlID) {
          releaseSeen = true;
        }

        // We halt receiving anything after the first control message
        if (errstr) {
          *errstr = "bad ID";
        }
        partialMsg.clear();
        return 1;
      }

      partialMsg.clear();
      buf.remove_prefix(nl + 1);
    }
    return 0;
  }

  bool shouldRelease() const { return releaseSeen; }
};

#if !defined(_WIN32) && defined(HAVE_POSIX_SPAWN)
// Helper function to collect subprocess output.
// Consumes and closes the outputPipe descriptor.
static void captureExecutedProcessOutput(ProcessDelegate& delegate,
                                         ManagedDescriptor& outputPipe,
                                         ProcessHandle handle,
                                         ProcessContext* ctx) {
  while (true) {
    char buf[4096];
    ssize_t numBytes =
        FileDescriptorTraits<>::Read(outputPipe.unsafeDescriptor(), buf, sizeof(buf));
    if (numBytes < 0) {
      int err = errno;
      delegate.processHadError(ctx, handle,
        makeLocalExecutorError(ExecutorError::IOError, sys::strerror(err)));
      break;
    }

    if (numBytes == 0)
      break;

    // Notify the client of the output.
    delegate.processHadOutput(ctx, handle, std::string_view(buf).substr(0, numBytes));
  }
  // We have receieved the zero byte read that indicates an EOF.
  // Go ahead and close the pipe (it was going to be closed automatically).
  outputPipe.close();
}
#endif

#if defined(_WIN32) || defined(HAVE_POSIX_SPAWN)
// Helper function for cleaning up after a process has finished in
// executeProcess
static void cleanUpExecutedProcess(ProcessDelegate& delegate,
                                   ProcessGroup& pgrp, llbuild_pid_t pid,
                                   ProcessHandle handle, ProcessContext* ctx,
                                   ProcessCompletionFn&& completionFn,
                                   ManagedDescriptor& releaseFd) {
#if defined(_WIN32)
  FILETIME creationTime;
  FILETIME exitTime;
  FILETIME utimeTicks;
  FILETIME stimeTicks;
  int waitResult = WaitForSingleObject(pid, INFINITE);
  int err = GetLastError();
  DWORD exitCode = 0;
  GetExitCodeProcess(pid, &exitCode);

  if (waitResult == WAIT_FAILED || waitResult == WAIT_ABANDONED) {
    releaseFd.close();
    auto result = ProcessResult::makeFailed(exitCode);
    delegate.processHadError(ctx, handle,
      makeLocalExecutorError(ExecutorError::WaitFailed,
                             sys::strerror(GetLastError())));
    delegate.processFinished(ctx, handle, result);
    completionFn(result);
    return;
  }
#else
  // Wait for the command to complete.
  struct rusage usage;
  int exitCode, result = wait4(pid, &exitCode, 0, &usage);
  while (result == -1 && errno == EINTR)
    result = wait4(pid, &exitCode, 0, &usage);
#endif
  // Close the release pipe
  //
  // Note: We purposely hold this open until after the process has finished as
  // it simplifies client implentation. If we close it early, clients need to be
  // aware of and potentially handle a SIGPIPE.
  releaseFd.close();

  // Update the set of spawned processes.
  pgrp.remove(pid);
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS counters;
  bool res =
      GetProcessTimes(pid, &creationTime, &exitTime, &stimeTicks, &utimeTicks);
  if (!res) {
    auto result = ProcessResult::makeCancelled();
    delegate.processHadError(ctx, handle,
      makeLocalExecutorError(ExecutorError::ProcessStatsError,
                             sys::strerror(GetLastError())));
    delegate.processFinished(ctx, handle, result);
    return;
  }
  // Each tick is 100ns
  uint64_t utime =
      ((uint64_t)utimeTicks.dwHighDateTime << 32 | utimeTicks.dwLowDateTime) /
      10;
  uint64_t stime =
      ((uint64_t)stimeTicks.dwHighDateTime << 32 | stimeTicks.dwLowDateTime) /
      10;
  GetProcessMemoryInfo(pid, &counters, sizeof(counters));

  // We report additional info in the tracing interval
  //   - user time, in µs
  //   - sys time, in µs
  //   - memory usage, in bytes

  // FIXME: We should report a statistic for how much output we read from the
  // subprocess (probably as a new point sample).

  // Notify of the process completion.
  ProcessStatus processStatus =
      (exitCode == 0) ? ProcessStatus::Succeeded : ProcessStatus::Failed;
  ProcessResult processResult(processStatus, exitCode, pid, utime, stime,
                              counters.PeakWorkingSetSize);
#else  // !defined(_WIN32)
  if (result == -1) {
    auto result = ProcessResult::makeFailed(exitCode);
    delegate.processHadError(ctx, handle,
      makeLocalExecutorError(ExecutorError::WaitFailed, strerror(errno)));
    delegate.processFinished(ctx, handle, result);
    completionFn(result);
    return;
  }

  // We report additional info in the tracing interval
  //   - user time, in µs
  //   - sys time, in µs
  //   - memory usage, in bytes
  uint64_t utime = (uint64_t(usage.ru_utime.tv_sec) * 1000000 +
                    uint64_t(usage.ru_utime.tv_usec));
  uint64_t stime = (uint64_t(usage.ru_stime.tv_sec) * 1000000 +
                    uint64_t(usage.ru_stime.tv_usec));

  // FIXME: We should report a statistic for how much output we read from the
  // subprocess (probably as a new point sample).

  // Notify of the process completion.
  bool cancelled = WIFSIGNALED(exitCode) && (WTERMSIG(exitCode) == SIGINT || WTERMSIG(exitCode) == SIGKILL);
  ProcessStatus processStatus = cancelled ? ProcessStatus::Cancelled : (exitCode == 0) ? ProcessStatus::Succeeded : ProcessStatus::Failed;
  ProcessResult processResult(processStatus, exitCode, pid, utime, stime,
                              usage.ru_maxrss);
#endif // else !defined(_WIN32)
  delegate.processFinished(ctx, handle, processResult);
  completionFn(processResult);
}
#endif

#if defined(_WIN32) || defined(HAVE_POSIX_SPAWN)
#if defined(_WIN32)
  using PlatformSpecificPipesConfig = STARTUPINFOW;
#else
  using PlatformSpecificPipesConfig = posix_spawn_file_actions_t;
#endif

/// Create all or no communication pipes.
enum class CommunicationPipesCreationError {
  ERROR_NONE,
  OUTPUT_PIPE_FAILED,
  CONTROL_PIPE_FAILED
};
static std::pair<CommunicationPipesCreationError, int> createCommunicationPipes(const ProcessAttributes &attr,
        PlatformSpecificPipesConfig& pipesConfig,
        ManagedDescriptor& outputPipeParentEnd,
        ManagedDescriptor& outputPipeChildEnd,
        ManagedDescriptor& controlPipeParentEnd,
        ManagedDescriptor& controlPipeChildEnd) {
#if defined(_WIN32)
  STARTUPINFOW& startupInfo = pipesConfig;
  startupInfo.dwFlags = STARTF_USESTDHANDLES;
  if (attr.connectToConsole) {
    // Connect to the current stdout/stderr.
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  } else {
    // Set NUL as stdin
    HANDLE nul =
      CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE outputPipe[2]{NULL, NULL};
    SECURITY_ATTRIBUTES secAttrs{sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (CreatePipe(&outputPipe[0], &outputPipe[1], &secAttrs, 0) == 0) {
      return std::make_pair(CommunicationPipesCreationError::OUTPUT_PIPE_FAILED, errno);
    }
    startupInfo.hStdInput = nul;
    startupInfo.hStdOutput = outputPipe[1];
    startupInfo.hStdError = outputPipe[1];
    outputPipeParentEnd.reset(outputPipe[0]).childMayInherit(false);
    outputPipeChildEnd.reset(outputPipe[1]);
  }

  if (attr.controlEnabled) {
    HANDLE controlPipe[2]{NULL, NULL};
    SECURITY_ATTRIBUTES secAttrs{sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (CreatePipe(&controlPipe[0], &controlPipe[1], &secAttrs, 0) == 0) {
      outputPipeParentEnd.close();
      outputPipeChildEnd.close();
      return std::make_pair(CommunicationPipesCreationError::OUTPUT_PIPE_FAILED, errno);
    }
    controlPipeParentEnd.reset(controlPipe[0]).childMayInherit(false);
    controlPipeChildEnd.reset(controlPipe[1]);
  }
#else

  posix_spawn_file_actions_t& fileActions = pipesConfig;

  // If we are capturing output, create a pipe and appropriate spawn actions.
  if (attr.connectToConsole) {
#ifdef __APPLE__
    posix_spawn_file_actions_addinherit_np(&fileActions, STDIN_FILENO);
#else
    posix_spawn_file_actions_adddup2(&fileActions, STDIN_FILENO, STDIN_FILENO);
#endif
    // Propagate the current stdout/stderr.
    posix_spawn_file_actions_adddup2(&fileActions, STDOUT_FILENO, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fileActions, STDERR_FILENO, STDERR_FILENO);
  } else {
    // Open /dev/null as stdin.
    posix_spawn_file_actions_addopen(&fileActions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    int outputPipe[2]{ -1, -1 };
    if (sys::pipe(outputPipe) < 0) {
      return std::make_pair(CommunicationPipesCreationError::OUTPUT_PIPE_FAILED, errno);
    }
    outputPipeParentEnd.reset(outputPipe[0]).childMayInherit(false);
    outputPipeChildEnd.reset(outputPipe[1]);

    // Open the write end of the pipe as stdout and stderr.
    // The code is safe.
    posix_spawn_file_actions_adddup2(&fileActions, outputPipeChildEnd.unsafeDescriptor(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fileActions, outputPipeChildEnd.unsafeDescriptor(), STDERR_FILENO);

    // Close the child end of the pipe known under a different number.
    posix_spawn_file_actions_addclose(&fileActions, outputPipeChildEnd.unsafeDescriptor());
  }

  // Create a pipe for the process to (potentially) release the lane while
  // still running.
  if (attr.controlEnabled) {
    int controlPipe[2]{ -1, -1 };
    if (sys::pipe(controlPipe) < 0) {
      outputPipeParentEnd.close();
      outputPipeChildEnd.close();
      return std::make_pair(CommunicationPipesCreationError::CONTROL_PIPE_FAILED, errno);
    }

    controlPipeParentEnd.reset(controlPipe[0]).childMayInherit(false);
    controlPipeChildEnd.reset(controlPipe[1]);

    // Make sure that the descriptor is properly inherited by the child.
    // The code is safe.
#ifdef __APPLE__
    posix_spawn_file_actions_addinherit_np(&fileActions, controlPipeChildEnd.unsafeDescriptor());
#else
    posix_spawn_file_actions_adddup2(&fileActions, controlPipeChildEnd.unsafeDescriptor(), controlPipeChildEnd.unsafeDescriptor());
#endif
  }

#endif

  return std::make_pair(CommunicationPipesCreationError::ERROR_NONE, 0);
}
#endif


/// Execute the given command line.
///
/// This will launch and execute the given command line and wait for it to
/// complete or release its execution lane.
///
/// \param delegate The process delegate.
///
/// \param ctx The context object passed to the delegate.
///
/// \param pgrp The process group in which to track this process.
///
/// \param handle The handle object passed to the delegate.
///
/// \param commandLine The command line to execute.
///
/// \param environment The environment to launch with.
///
/// \param attributes Additional attributes for the process to be spawned.
///
/// \param releaseFn Functional called when a process wishes to release its
/// exclusive access to build system resources (namely an execution lane).
///
/// \param completionFn An optional function that, if supplied, will be run
/// following the completion of the process. This may be run asynchronously
/// from another thread if the executed process asks the system to release
/// its execution lane. Callers should put cleanup and notification work
/// here.
///

typedef std::function<void(std::function<void()>&&)> ProcessReleaseFn;

void spawnProcess(
    ProcessDelegate& delegate,
    ProcessContext* ctx,
    ProcessGroup& pgrp,
    ProcessHandle handle,
    std::vector<std::string_view>& commandLine,
    POSIXEnvironment environment,
    ProcessAttributes attr,
    ProcessReleaseFn&& releaseFn,
    ProcessCompletionFn&& completionFn
) {
  llbuild_pid_t pid = (llbuild_pid_t)-1;

#if !defined(_WIN32) && !defined(HAVE_POSIX_SPAWN)
  auto result = ProcessResult::makeFailed();
  delegate.processStarted(ctx, handle, pid);
  delegate.processHadError(ctx, handle,
    makeLocalExecutorError(ExecutorError::ProcessSpawnFailed, "unavailable"));
  delegate.processFinished(ctx, handle, result);
  completionFn(result);
  return;
#else
  // Don't use lane release feature for console workloads.
  if (attr.connectToConsole) {
    attr.controlEnabled = false;
  }

#if defined(_WIN32)
  // Control channel support is broken (thread-unsafe) on Windows.
  attr.controlEnabled = false;
#endif

  if (commandLine.size() == 0) {
    auto result = ProcessResult::makeFailed();
    delegate.processStarted(ctx, handle, pid);
    delegate.processHadError(ctx, handle,
      makeLocalExecutorError(ExecutorError::ProcessSpawnFailed, "no arguments for command"));
    delegate.processFinished(ctx, handle, result);
    completionFn(result);
    return;
  }

  // Form the complete C string command line.
  std::vector<std::string> argsStorage(commandLine.begin(), commandLine.end());
#if defined(_WIN32)
  std::string args = llbuild::basic::formatWindowsCommandString(argsStorage);

  // Convert the command line string to utf16
  llvm::SmallVector<llvm::UTF16, 20> u16Executable;
  llvm::SmallVector<llvm::UTF16, 20> u16CmdLine;
  llvm::convertUTF8ToUTF16String(argsStorage[0], u16Executable);
  llvm::convertUTF8ToUTF16String(args, u16CmdLine);
#else
  std::vector<const char*> args(argsStorage.size() + 1);
  for (size_t i = 0; i != argsStorage.size(); ++i) {
    args[i] = argsStorage[i].c_str();
  }
  args[argsStorage.size()] = nullptr;
#endif

#if defined(_WIN32)
  DWORD creationFlags = NORMAL_PRIORITY_CLASS |
                        CREATE_UNICODE_ENVIRONMENT;
  PROCESS_INFORMATION processInfo = {0};

#else
  // Initialize the spawn attributes.
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);

  // Unmask all signals.
  sigset_t noSignals;
  sigemptyset(&noSignals);
  posix_spawnattr_setsigmask(&attributes, &noSignals);

  // Reset all signals to default behavior.
  //
  // On Linux, this can only be used to reset signals that are legal to
  // modify, so we have to take care about the set we use.
#if defined(__linux__)
  sigset_t mostSignals;
  sigemptyset(&mostSignals);
  for (int i = 1; i < SIGSYS; ++i) {
    if (i == SIGKILL || i == SIGSTOP) continue;
    sigaddset(&mostSignals, i);
  }
  posix_spawnattr_setsigdefault(&attributes, &mostSignals);
#else
  sigset_t mostSignals;
  sigfillset(&mostSignals);
  sigdelset(&mostSignals, SIGKILL);
  sigdelset(&mostSignals, SIGSTOP);
  posix_spawnattr_setsigdefault(&attributes, &mostSignals);
#endif // else !defined(_WIN32)

  // Establish a separate process group.
  posix_spawnattr_setpgroup(&attributes, 0);

  // Set the attribute flags.
  unsigned flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
  if (!attr.connectToConsole) {
    flags |= POSIX_SPAWN_SETPGROUP;
  }

  // Close all other files by default.
  //
  // FIXME: Note that this is an Apple-specific extension, and we will have to
  // do something else on other platforms (and unfortunately, there isn't
  // really an easy answer other than using a stub executable).
#ifdef __APPLE__
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif

  // On Darwin, set the QoS of launched processes to one of the current thread.
#ifdef __APPLE__
  posix_spawnattr_set_qos_class_np(&attributes, qos_class_self());
#endif

  posix_spawnattr_setflags(&attributes, flags);

  // Setup the file actions.
  posix_spawn_file_actions_t fileActions;
  posix_spawn_file_actions_init(&fileActions);

  bool usePosixSpawnChdirFallback = true;
  const auto workingDir = attr.workingDir;
  if (!workingDir.empty() &&
      posix_spawn_file_actions_addchdir_polyfill(&fileActions, workingDir.c_str()) != ENOSYS) {
    usePosixSpawnChdirFallback = false;
  }

#endif

#if defined(_WIN32)
  /// Process startup information for Windows.
  STARTUPINFOW startupInfo = {0};
  PlatformSpecificPipesConfig& pipesConfig = startupInfo;
#else
  PlatformSpecificPipesConfig& pipesConfig = fileActions;
#endif

  // Automatically managed (released) descriptors for output and control pipes.
  // The child ends are forwarded to the child (and quickly released in the
  // parent). The parent ends are retained and read/written by the parent.
  ManagedDescriptor outputPipeParentEnd{__FILE__, __LINE__};
  ManagedDescriptor controlPipeParentEnd{__FILE__, __LINE__};

#if defined(_WIN32)
  llvm::SmallVector<llvm::UTF16, 20> u16Cwd;
  std::string workingDir = attr.workingDir.str();
  if (!workingDir.empty()) {
    llvm::convertUTF8ToUTF16String(workingDir, u16Cwd);
  }
#endif

  // Export a task ID to subprocesses.
  auto taskID = std::format("{:x}", handle.id);
  environment.setIfMissing("LLBUILD_TASK_ID", taskID);

  // Resolve the executable path, if necessary.
  //
  // FIXME: This should be cached.
  if (!std::filesystem::path(argsStorage[0]).is_absolute()) {
    auto res = sys::findProgramByName(argsStorage[0]);
    if (!res.has_error()) {
      argsStorage[0] = *res;
#if defined(_WIN32)
      u16Executable.clear();
      llvm::convertUTF8ToUTF16String(argsStorage[0], u16Executable);
#else
      args[0] = argsStorage[0].c_str();
#endif
    }
  }

  // Spawn the command.
  bool wasCancelled;
  do {
      // We need to hold the spawn processes lock when we spawn, to ensure that
      // we don't create a process in between when we are cancelled.
      std::lock_guard<std::mutex> guard(pgrp.mutex);
      wasCancelled = pgrp.isClosed();

      // If we have been cancelled since we started, skip startup.
      if (wasCancelled) { break; }

      // The partf of the control pipes that are inherited by the child.
      ManagedDescriptor outputPipeChildEnd{__FILE__, __LINE__};
      ManagedDescriptor controlPipeChildEnd{__FILE__, __LINE__};

      // Open the communication channel under the mutex to avoid
      // leaking the wrong channel into other children started concurrently.
      auto errorPair = createCommunicationPipes(attr, pipesConfig, outputPipeParentEnd, outputPipeChildEnd, controlPipeParentEnd, controlPipeChildEnd);
      if (errorPair.first != CommunicationPipesCreationError::ERROR_NONE) {
        std::string whatPipe = errorPair.first == CommunicationPipesCreationError::OUTPUT_PIPE_FAILED ? "output pipe" : "control pipe";
#if !defined(_WIN32)
        posix_spawn_file_actions_destroy(&fileActions);
        posix_spawnattr_destroy(&attributes);
#endif
        delegate.processStarted(ctx, handle, pid);
        delegate.processHadError(ctx, handle,
          makeLocalExecutorError(ExecutorError::IOError,
                                 "unable to open " + whatPipe + " (" +
                                 sys::strerror(errorPair.second) + ")"));
        delegate.processFinished(ctx, handle, ProcessResult::makeFailed());
        completionFn(ProcessResult(ProcessStatus::Failed));
        return;
      }

      if (controlPipeChildEnd.isValid()) {
        long long controlFd = (long long)controlPipeChildEnd.unsafeDescriptor();
        environment.setIfMissing("LLBUILD_CONTROL_FD", std::to_string(controlFd));
      }

      int result = 0;

      bool workingDirectoryUnsupported = false;

#if !defined(_WIN32)
      if (usePosixSpawnChdirFallback) {
#if defined(__APPLE__)
        thread_local std::string threadWorkingDir;

        if (workingDir.empty()) {
          if (!threadWorkingDir.empty()) {
            pthread_fchdir_np(-1);
            threadWorkingDir.clear();
          }
        } else {
          if (threadWorkingDir != workingDir) {
            if (pthread_chdir_np(workingDir.c_str()) == -1) {
              result = errno;
            } else {
              threadWorkingDir = workingDir;
            }
          }
        }
#else
        if (!workingDir.empty()) {
          workingDirectoryUnsupported = true;
          result = -1;
        }
#endif // if defined(__APPLE__)
      }
#endif // else !defined(_WIN32)

      if (result == 0) {
#if defined(_WIN32)
        auto unicodeEnv = environment.getWindowsEnvp();
        result = !CreateProcessW(
            /*lpApplicationName=*/(LPWSTR)u16Executable.data(),
            (LPWSTR)u16CmdLine.data(),
            /*lpProcessAttributes=*/NULL,
            /*lpThreadAttributes=*/NULL,
            /*bInheritHandles=*/TRUE, creationFlags,
            /*lpEnvironment=*/unicodeEnv.get(),
            /*lpCurrentDirectory=*/u16Cwd.empty() ? NULL
                                                  : (LPWSTR)u16Cwd.data(),
            &startupInfo, &processInfo);
#else
        result =
            posix_spawn(&pid, args[0], /*file_actions=*/&fileActions,
                        /*attrp=*/&attributes, const_cast<char**>(args.data()),
                        const_cast<char* const*>(environment.getEnvp()));
#endif
      }

      delegate.processStarted(ctx, handle, pid);

      if (result != 0) {
        auto processResult = ProcessResult::makeFailed();
#if defined(_WIN32)
        result = GetLastError();
#endif
        delegate.processHadError(
          ctx, handle,
          makeLocalExecutorError(ExecutorError::ProcessSpawnFailed,
            workingDirectoryUnsupported
              ? "working-directory unsupported on this platform"
              : sys::strerror(result)));
        delegate.processFinished(ctx, handle, processResult);
        pid = (llbuild_pid_t)-1;
      } else {
#if defined(_WIN32)
        pid = processInfo.hProcess;
#endif
        ProcessInfo info{ attr.canSafelyInterrupt };
        pgrp.add(std::move(guard), pid, info);
      }

    // Close the child ends of the forwarded output and control pipes.
    controlPipeChildEnd.close();
    outputPipeChildEnd.close();
  } while(false);

#if !defined(_WIN32)
  posix_spawn_file_actions_destroy(&fileActions);
  posix_spawnattr_destroy(&attributes);
#endif

  // If we failed to launch a process, clean up and abort.
  if (pid == (llbuild_pid_t)-1) {
    // Manually close to avoid triggering debug-time leak check.
    outputPipeParentEnd.close();
    controlPipeParentEnd.close();
    auto result = wasCancelled ? ProcessResult::makeCancelled() : ProcessResult::makeFailed();
    completionFn(result);
    return;
  }

#if !defined(_WIN32)
  // Set up our poll() structures. We use assert() to ensure
  // the file descriptors are alive.
  pollfd readfds[] = {
    { outputPipeParentEnd.unsafeDescriptor(), 0, 0 },
    { controlPipeParentEnd.unsafeDescriptor(), 0, 0 }
  };
  bool activeEvents = false;
#endif
  const int nfds = 2;
  ControlProtocolState control(taskID);
  std::function<bool (std::string_view)> readCbs[] = {
    // output capture callback
    [&delegate, ctx, handle](std::string_view buf) -> bool {
      // Notify the client of the output.
      delegate.processHadOutput(ctx, handle, buf);
      return true;
    },
    // control callback handle
    [&delegate, &control, ctx, handle](std::string_view buf) mutable -> bool {
      std::string errstr;
      size_t ret = control.read(buf, &errstr);
      if (ret < 0) {
        delegate.processHadError(ctx, handle,
          makeLocalExecutorError(ExecutorError::ControlProtocolError, errstr));
      }
      return (ret == 0);
    }
  };
#if defined(_WIN32)
  struct threadData {
    std::function<void(void*)> reader;
    const ManagedDescriptor& handle;
    std::function<bool(StringRef)> cb;
  };
  HANDLE readers[2] = {NULL, NULL};
  auto reader = [&delegate, handle, ctx](void* lpArgs) {
    threadData* args = (threadData*)lpArgs;
    for (;;) {
      char buf[4096];
      DWORD numBytes;
      bool result = ReadFile(args->handle.unsafeDescriptor(), buf, sizeof(buf), &numBytes, NULL);

      if (!result || numBytes == 0) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
          // Pipe done, exit
          return;
        } else {
          delegate.processHadError(ctx, handle,
            makeLocalExecutorError(ExecutorError::IOError, sys::strerror(GetLastError())));
        }
      }

      if (numBytes <= 0 || !args->cb(StringRef(buf, numBytes))) {
        continue;
      }
    }
  };

  struct threadData outputThreadParams = {reader, outputPipeParentEnd, readCbs[0]};
  struct threadData controlThreadParams = {reader, controlPipeParentEnd, readCbs[1]};
  HANDLE threads[2] = {NULL, NULL};
#endif // defined(_WIN32)

  int threadCount = 0;

  // Read the command output, if capturing instead of pass-throughing.
  if (!attr.connectToConsole) {
#if defined(_WIN32)
    threads[threadCount++] = (HANDLE)_beginthread(
        [](LPVOID lpParams) { ((threadData*)lpParams)->reader(lpParams); }, 0,
        &outputThreadParams);
#else
    readfds[0].events = POLLIN;
    activeEvents = true;
    (void)threadCount;
#endif
  }

  // Process the control channel input.
  if (attr.controlEnabled) {
#if defined(_WIN32)
    threads[threadCount++] = (HANDLE)_beginthread(
        [](LPVOID lpParams) { ((threadData*)lpParams)->reader(lpParams); }, 0,
        &controlThreadParams);
#else
    readfds[1].events = POLLIN;
    activeEvents = true;
#endif
  }

#if defined(_WIN32)
  DWORD waitResult = WaitForMultipleObjects(threadCount, threads,
                                            /*bWaitAll=*/false,
                                            /*dwMilliseconds=*/INFINITE);
  if (WAIT_FAILED == waitResult || WAIT_TIMEOUT == waitResult) {
    int err = GetLastError();
    delegate.processHadError(ctx, handle,
      makeLocalExecutorError(ExecutorError::IOError,
                             "failed to poll (" + sys::strerror(err) + ")"));
  }
#else  // !defined(_WIN32)
  while (activeEvents) {
    char buf[4096];
    activeEvents = false;

    // Ensure we haven't autoclosed the file descriptors,
    // or move()d somewhere they could be autoclosed in.
    assert(readfds[0].fd == outputPipeParentEnd.unsafeDescriptor());
    assert(readfds[1].fd == controlPipeParentEnd.unsafeDescriptor());

    while (poll(readfds, nfds, -1) == -1) {
        int err = errno;

        if (err == EAGAIN || err == EINTR) {
          continue;
        } else {
          delegate.processHadError(ctx, handle,
            makeLocalExecutorError(ExecutorError::IOError,
                                   "failed to poll (" + sys::strerror(err) + ")"));
          return;
        }
    }

    for (int i = 0; i < nfds; i++) {
      if (readfds[i].revents & (POLLIN | POLLERR | POLLHUP)) {
        ssize_t numBytes = read(readfds[i].fd, buf, sizeof(buf));
        if (numBytes < 0) {
          int err = errno;
          delegate.processHadError(ctx, handle,
            makeLocalExecutorError(ExecutorError::IOError, sys::strerror(err)));
        }
        if (numBytes <= 0 || !readCbs[i](std::string_view(buf).substr(0, numBytes))) {
          readfds[i].events = 0;
          continue;
        }
      }
      activeEvents |= readfds[i].events != 0;
    }

    if (control.shouldRelease()) {
      std::shared_ptr<ManagedDescriptor> outputFdShared
        = std::make_shared<ManagedDescriptor>(std::move(outputPipeParentEnd));
      std::shared_ptr<ManagedDescriptor> controlFdShared
        = std::make_shared<ManagedDescriptor>(std::move(controlPipeParentEnd));
      releaseFn([&delegate, &pgrp, pid, handle, ctx,
                 outputFdShared, controlFdShared,
                 completionFn=std::move(completionFn)]() mutable {
        if (outputFdShared->isValid()) {
          captureExecutedProcessOutput(delegate, *outputFdShared, handle, ctx);
        }
        cleanUpExecutedProcess(delegate, pgrp, pid, handle, ctx,
                               std::move(completionFn), *controlFdShared);
      });
      return;
    }

  }
#endif // else !defined(_WIN32)
  // If we have reached here, both the control and read pipes have given us
  // the requisite EOF/hang-up events. Safe to close the read end of the
  // output pipe.
  outputPipeParentEnd.close();
  cleanUpExecutedProcess(delegate, pgrp, pid, handle, ctx,
                         std::move(completionFn), controlPipeParentEnd);
#endif
}

class LocalExecutorImpl {
private:
  std::shared_ptr<LocalSandboxProvider> sandboxProvider;

  ProcessGroup spawnedProcesses;
  bool cancelled { false };

  /// Management of cancellation and SIGKILL escalation
  std::mutex killAfterTimeoutThreadMutex;
  std::unique_ptr<std::thread> killAfterTimeoutThread = nullptr;
  std::condition_variable shutdownCompleteCondition;
  std::mutex shutdownCompleteMutex;
  bool shutdownComplete { false };

  /// Background (lane released) task management
  unsigned backgroundTaskMax = 0;
  std::atomic<unsigned> backgroundTaskCount{0};

  /// The base environment.
  const char* const* environment;

  void killAfterTimeout() {
    std::unique_lock<std::mutex> lock(shutdownCompleteMutex);

    if (!shutdownComplete) {
      // Shorten timeout if in testing context
      if (getenv("LLBUILD_TEST") != nullptr) {
        shutdownCompleteCondition.wait_for(lock, std::chrono::milliseconds(1000));
      } else {
        shutdownCompleteCondition.wait_for(lock, std::chrono::seconds(10));
      }

#if _WIN32
      spawnedProcesses.signalAll(SIGTERM);
#else
      spawnedProcesses.signalAll(SIGKILL);
#endif
    }
  }

public:
  LocalExecutorImpl(std::shared_ptr<LocalSandboxProvider> sandboxProvider)
    : sandboxProvider(sandboxProvider) {

  }

  virtual ~LocalExecutorImpl() {
    std::lock_guard<std::mutex> guard(killAfterTimeoutThreadMutex);
    if (killAfterTimeoutThread) {
      {
        std::unique_lock<std::mutex> lock(shutdownCompleteMutex);
        shutdownComplete = true;
        shutdownCompleteCondition.notify_all();
      }
      killAfterTimeoutThread->join();
    }
  }

  result<std::shared_ptr<LocalSandbox>, Error>
  createSandbox(ProcessHandle hndl) {
    return sandboxProvider->create(hndl);
  }

  void executeProcess(
    std::vector<std::string_view>& commandLine,
    std::vector<std::pair<std::string_view, std::string_view>>& environment,
    ProcessDelegate& delegate,
    ProcessContext* ctx,
    ProcessHandle handle,
    ProcessAttributes attributes = {true},
    std::optional<ProcessCompletionFn> completionFn = {}
  ) {
    {
      std::unique_lock<std::mutex> lock(spawnedProcesses.mutex);
      // Do not execute new processes anymore after cancellation.
      if (cancelled) {
        if (completionFn.has_value())
          (*completionFn)(ProcessResult::makeCancelled());
        return;
      }
    }

    // Form the complete environment.
    //
    // NOTE: We construct the environment in order of precedence, so
    // overridden keys should be defined first.
    POSIXEnvironment posixEnv;

    // Add the requested environment.
    for (const auto& entry: environment) {
      posixEnv.setIfMissing(entry.first, entry.second);
    }

    // Inherit the base environment, if desired.
    //
    // FIXME: This involves a lot of redundant allocation, currently. We could
    // cache this for the common case of a directly inherited environment.
    if (attributes.inheritEnvironment) {
      for (const char* const* p = this->environment; *p != nullptr; ++p) {
        auto pair = split(*p, "=");
        posixEnv.setIfMissing(pair.first, pair.second);
      }
    }

    ProcessReleaseFn releaseFn = [this](std::function<void()>&& processWait) {
      auto previousTaskCount = backgroundTaskCount.fetch_add(1);
      if (previousTaskCount < backgroundTaskMax) {
        // Launch the process wait on a detached thread
        std::thread([this, processWait=std::move(processWait)]() mutable {
          processWait();
          backgroundTaskCount--;
        }).detach();
      } else {
        backgroundTaskCount--;
        // not allowed to release, call wait directly
        processWait();
      }
    };

    ProcessCompletionFn completeFn{
      [completionFn](ProcessResult result) mutable {
        if (completionFn.has_value())
          (*completionFn)(result);
      }
    };

    spawnProcess(
        delegate,
        ctx,
        spawnedProcesses,
        handle,
        commandLine,
        posixEnv,
        attributes,
        std::move(releaseFn),
        std::move(completeFn)
    );
  }

  void cancelAllJobs() {
    {
      std::lock_guard<std::mutex> guard(spawnedProcesses.mutex);
      if (cancelled) return;
      cancelled = true;
      spawnedProcesses.close();
    }

    spawnedProcesses.signalAll(SIGINT);
    {
      std::lock_guard<std::mutex> guard(killAfterTimeoutThreadMutex);
      killAfterTimeoutThread = std::unique_ptr<std::thread>(new std::thread(
          &LocalExecutorImpl::killAfterTimeout, this));
    }
  }
};

}

LocalSandbox::~LocalSandbox() { }
LocalSandboxProvider::~LocalSandboxProvider() { }

LocalExecutor::LocalExecutor(
  std::shared_ptr<LocalSandboxProvider> sandboxProvider
) : impl(new LocalExecutorImpl(std::move(sandboxProvider))) {

}

LocalExecutor::~LocalExecutor() {
  delete static_cast<LocalExecutorImpl*>(impl);
}

result<std::shared_ptr<LocalSandbox>, Error>
LocalExecutor::createSandbox(ProcessHandle hndl) {
  return static_cast<LocalExecutorImpl*>(impl)->createSandbox(hndl);
}

void LocalExecutor::executeProcess(
  std::vector<std::string_view>& commandLine,
  std::vector<std::pair<std::string_view, std::string_view>>& environment,
  ProcessDelegate& delegate,
  ProcessContext* ctx,
  ProcessHandle handle,
  ProcessAttributes attributes,
  std::optional<ProcessCompletionFn> completionFn
) {
  static_cast<LocalExecutorImpl*>(impl)->executeProcess(commandLine,
                                                        environment,
                                                        delegate,
                                                        ctx,
                                                        handle,
                                                        attributes,
                                                        completionFn);
}

void LocalExecutor::cancelAllJobs() {
  static_cast<LocalExecutorImpl*>(impl)->cancelAllJobs();
}

ProcessDelegate::~ProcessDelegate() { }
