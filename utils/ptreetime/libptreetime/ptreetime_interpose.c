//===- ptreetime_interpose.c ----------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

/* This file defines a simple interposition library which is intended for use
 * with `ptreetime`.
 *
 * The library is not intended to be used directly, rather it is automatically
 * inserted using ``DYLD_INSERT_LIBRARIES``.
 *
 * The library itself interpositions on ``_exit``, ``execve``, and
 * ``posix_spawn``. The library also inserts a static constructor which will be
 * run at load time. Due to the way ``dyld`` executes static constructors, the
 * startup constructor will be run after all of ``libSystem``'s initializers
 * (because the library itself needs to load libSystem), but will always be run
 * before any of the primary application constructors.
 *
 * The library logs various events to an output file specified by the
 * ``PTREETIME_LOG_PATH`` environment variable. The events are written with
 * timestamps defined by gettimeofday(), which allows the events to be properly
 * ordered with respect to dtrace events.
 */

#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#define DYLD_INTERPOSE(_src,_dst) \
   __attribute__((used)) static struct { \
     const void* src; \
     const void* dst; \
   } _interpose_##_dst __attribute__ ((section ("__DATA,__interpose"))) = { \
     (const void*)(uintptr_t)&_src, \
     (const void*)(uintptr_t)&_dst \
   };

const char *ptreetime_log_path = 0;
static int ptreetime_log_file = -1;

static void error(const char *fmt, ...) {
  /* Get the current time of day. */
  struct timeval t;
  gettimeofday(&t, NULL);

  char buffer[1024];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);

  fprintf(stderr,
          "PTREETIME { \"ts\" : %lld, \"error\" : '%s', \"errno\" : %d }\n",
          (uint64_t) t.tv_sec * 1000000 + t.tv_usec, buffer, errno);
}

static void ptreetime_write_buffer(const char *buffer, int size,
                                   int allow_reopen) {
  /* If the log file hasn't been opened, open it. */
  if (ptreetime_log_file == -1) {
    /* Only log if the PTREETIME_LOG_PATH environment variable is set. */
    ptreetime_log_path = getenv("PTREETIME_LOG_PATH");
    if (!ptreetime_log_path) {
      errno = 0;
      error("no PTREETIME_LOG_PATH variable set");
      return;
    }

    ptreetime_log_file = open(ptreetime_log_path, O_WRONLY|O_CREAT|O_APPEND,
                              0664);
    if (ptreetime_log_file == -1) {
      error("unable to open log file '%s' (%s)", ptreetime_log_path,
            strerror(errno));
      return;
    }
  }

  struct iovec iov = { (void*) buffer, size };
  if (writev(ptreetime_log_file, &iov, 1) == -1) {
    /* If the write failed with a bad file descriptor, retry with a reopen, in
       case someone decided to close our output file descriptor. */
    if (allow_reopen && errno == EBADF) {
      ptreetime_log_file = -1;
      ptreetime_write_buffer(buffer, size, /*allow_reopen=*/0);
      return;
    }

    /* Otherwise, report the error. */
      error("unable to write to log file '%s' (%s)", ptreetime_log_path,
            strerror(errno));
  }
}

static void write_log_entry(const char *event) {
  /* Get the program's resource usage. */
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);

  /* Get the current time of day. */
  struct timeval t;
  gettimeofday(&t, NULL);

  /* Write the log event. */
  char buffer[1024];
  int size = snprintf(buffer, sizeof(buffer),
                      ("PTREETIME { \"ts\" : %lld, \"evt\" : \"%-14s\", "
                       "\"pid\" : %d, \"parent\" : %d, "
                       "\"utime\" : [%ld, %d], \"stime\" : [%ld, %d] }\n"),
                      (uint64_t) t.tv_sec * 1000000 + t.tv_usec, event,
                      getpid(), getppid(),
                      usage.ru_utime.tv_sec, usage.ru_utime.tv_usec,
                      usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);

  /* Use writev to get an atomic write. */
  ptreetime_write_buffer(buffer, size, /*allow_reopen=*/1);
}

static int clean_arg(const char *arg, char *dst, unsigned dst_size) {
  unsigned pos = 0;

  for (unsigned i = 0; ; ++i) {
    char c = arg[i];

    /* If we have an unclean character, escape it. */
    if (arg[i] == '\\' || arg[i] == '"') {
      if (pos == dst_size)
        return 0;
      dst[pos++] = '\\';
    }

    if (pos == dst_size)
      return 0;
    dst[pos++] = c;

    if (c == '\0')
      return 1;
  }
}

static void write_create_log_entry(const char *event, char *const argv[]) {
  /* Get the program's resource usage. */
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);

  /* Get the current time of day. */
  struct timeval t;
  gettimeofday(&t, NULL);

  /* Write the log event start. */
  char buffer[4096];
  int size = snprintf(buffer, sizeof(buffer),
                      ("PTREETIME { \"ts\" : %lld, \"evt\" : \"%-14s\", "
                       "\"pid\" : %d, \"parent\" : %d, "
                       "\"utime\" : [%ld, %d], \"stime\" : [%ld, %d], "
                       "\"args\" : ["),
                      (uint64_t) t.tv_sec * 1000000 + t.tv_usec, event,
                      getpid(), getppid(),
                      usage.ru_utime.tv_sec, usage.ru_utime.tv_usec,
                      usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);

  if (size >= sizeof(buffer))
    abort();

  /* Write the arguments (or as many will fit). */
  unsigned safe_space = strlen("\"...\"") + strlen("] }\n");
  for (unsigned i = 0; argv[i]; ++i) {
    /* Clean the argument, to escape any characters for JSON. */
    char arg_buffer[1024];
    if (!clean_arg(argv[i], arg_buffer, sizeof(arg_buffer))) {
      size += snprintf(&buffer[size], sizeof(buffer) - size, "\"...\"");
      break;
    }

    /* Write the argument into the buffer, if we have enough room. */
    int bytes = snprintf(&buffer[size], sizeof(buffer) - size - safe_space,
                         "\"%s\"%s", arg_buffer, argv[i+1] ? ", " : "");

    /* If we are out of space, break. */
    if (bytes >= sizeof(buffer) - size - safe_space) {
      size += snprintf(&buffer[size], sizeof(buffer) - size, "\"...\"");
      break;
    }

    size += bytes;
  }

  /* Write the log event end. */
  size += snprintf(&buffer[size], sizeof(buffer) - size, "] }\n");

  /* Write the event. */
  ptreetime_write_buffer(buffer, size, /*allow_reopen=*/1);
}

void interposed_exit(int status) {
  write_log_entry("user exit");

  return _exit(status);
}
DYLD_INTERPOSE(interposed_exit, _exit);

static void interposed_startup() \
  __attribute__((used)) __attribute__((constructor));
static void interposed_startup() {
  write_log_entry("user startup");
}

int interposed_execve(const char *path, char *const argv[],
                      char *const envp[]) {
  /* On Darwin, execvp will call execve in a loop. Only write the log entry if
     we see we have execute permission on the file. */
  if (access(path, X_OK) == 0)
    write_create_log_entry("user exec", argv);

  return execve(path, argv, envp);
}
DYLD_INTERPOSE(interposed_execve, execve);

int interposed_posix_spawn(pid_t *restrict pid, const char *restrict path,
                           const posix_spawn_file_actions_t *file_actions,
                           const posix_spawnattr_t *restrict attrp,
                           char *const argv[],
                           char *const envp[restrict]) {
  write_create_log_entry("user spawn", argv);
  return posix_spawn(pid, path, file_actions, attrp, argv, envp);
}
DYLD_INTERPOSE(interposed_posix_spawn, posix_spawn);

