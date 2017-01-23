// This is not a real config.

#ifndef LLBUILD_LLVM_CONFIG_H
#define LLBUILD_LLVM_CONFIG_H

#include "llvm/Config/llvm-config.h"

#if !defined(__has_include)
#define __has_include(x) 0
#endif

/* Define if you want backtraces on crash */
#define ENABLE_BACKTRACES

/* Define to enable crash overrides */
#undef ENABLE_CRASH_OVERRIDES

/* Define to 1 if you have the `arc4random' function. */
#if defined(__APPLE__)
#define HAVE_DECL_ARC4RANDOM 1
#else
/*#undef HAVE_DECL_ARC4RANDOM */
#endif

/* Define to 1 if you have the `backtrace' function. */
#define HAVE_BACKTRACE 1

/* Define to 1 if you have the <cxxabi.h> header file. */
#define HAVE_CXXABI_H 1

/* Define to 1 if you have the declaration of `strerror_s', and to 0 if you
   don't. */
#if defined(LLVM_ON_WIN32)
#define HAVE_DECL_STRERROR_S 1
#else
#define HAVE_DECL_STRERROR_S 0
#endif

/* Define to 1 if you have the DIA SDK installed, and to 0 if you don't. */
/* #undef HAVE_DIA_SDK */

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'.
   */
#define HAVE_DIRENT_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define if dlopen() is available on this platform. */
#define HAVE_DLOPEN 1

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the <execinfo.h> header file. */
#define HAVE_EXECINFO_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `futimes' function. */
#define HAVE_FUTIMES 1

/* Define to 1 if you have the `futimens' function */
/* #undef HAVE_FUTIMENS */

/* Define to 1 if you have the `getpagesize' function. */
#define HAVE_GETPAGESIZE 1

/* Define to 1 if you have the `getrlimit' function. */
#define HAVE_GETRLIMIT 1

/* Define to 1 if you have the `getrusage' function. */
#define HAVE_GETRUSAGE 1

/* Define to 1 if the system has the type `int64_t'. */
#define HAVE_INT64_T 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `isatty' function. */
#define HAVE_ISATTY 1

/* Define to 1 if you have the <link.h> header file. */
/* #undef HAVE_LINK_H */

/* Define to 1 if you have the <mach/mach.h> header file. */
#if __has_include(<mach/mach.h>)
#define HAVE_MACH_MACH_H 1
#else
/* #undef HAVE_MACH_MACH_H */
#endif

/* Define if mallinfo() is available on this platform. */
#if __has_include(<mallinfo.h>)
#define HAVE_MALLINFO 1
#else
/* #undef HAVE_MALLINFO */
#endif

/* Define to 1 if you have the <malloc.h> header file. */
#if __has_include(<malloc.h>)
#define HAVE_MALLOC_H
#else
/* #undef HAVE_MALLOC_H */
#endif

/* Define to 1 if you have the <malloc/malloc.h> header file. */
#if __has_include(<malloc/malloc.h>)
#define HAVE_MALLOC_MALLOC_H 1
#else
/* #undef HAVE_MALLOC_MALLOC_H */
#endif

/* Define to 1 if you have the `malloc_zone_statistics' function. */
#define HAVE_MALLOC_ZONE_STATISTICS 1

/* Define to 1 if you have the `mallctl` function. */
/* #undef HAVE_MALLCTL */

/* Define to 1 if you have a working `mmap' system call. */
#undef HAVE_MMAP

/* Define if mmap() uses MAP_ANONYMOUS to map anonymous pages, or undefine if
   it uses MAP_ANON */
#undef HAVE_MMAP_ANONYMOUS

/* Define if mmap() can map files into memory */
#undef HAVE_MMAP_FILE

/* Define to 1 if you have the `posix_spawn' function. */
#define HAVE_POSIX_SPAWN 1

/* Define to 1 if you have the `pread' function. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_PREAD 1
#endif

/* Define to 1 if you have the <pthread.h> header file. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_PTHREAD_H 1
#endif

/* Have pthread_mutex_lock */
#if !defined(LLVM_ON_WIN32)
#define HAVE_PTHREAD_MUTEX_LOCK 1
#endif

/* Define to 1 if you have the `sbrk' function. */
#define HAVE_SBRK 1

/* Define to 1 if you have the `setrlimit' function. */
#define HAVE_SETRLIMIT 1

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the `strerror' function. */
#define HAVE_STRERROR 1

/* Define to 1 if you have the `strerror_r' function. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_STRERROR_R 1
#endif

/* Define to 1 if you have the `sysconf' function. */
#undef HAVE_SYSCONF

/* Define to 1 if you have the <sys/dir.h> header file, and it defines `DIR'.
   */
#define HAVE_SYS_DIR_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 

/* Define to 1 if you have the <sys/ndir.h> header file, and it defines `DIR'.
   */
/* #undef HAVE_SYS_NDIR_H */

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/resource.h> header file. */
#define HAVE_SYS_RESOURCE_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_SYS_TIME_H 1
#endif

/* Define to 1 if you have the <sys/uio.h> header file. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_SYS_UIO_H 1
#endif

/* Define to 1 if you have <sys/wait.h> that is POSIX.1 compatible. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_SYS_WAIT_H 1
#endif

/* Define if the setupterm() function is supported this platform. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_TERMINFO 1
#endif

/* Define to 1 if you have the <termios.h> header file. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_TERMIOS_H 1
#endif

/* Define to 1 if the system has the type `uint64_t'. */
#define HAVE_UINT64_T 1

/* Define to 1 if you have the <unistd.h> header file. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_UNISTD_H 1
#endif

/* Define to 1 if the system has the type `u_int64_t'. */
#define HAVE_U_INT64_T 1

/* Define to 1 if you have the <valgrind/valgrind.h> header file. */
/* #undef HAVE_VALGRIND_VALGRIND_H */

/* Define to 1 if you have the `writev' function. */
#if !defined(LLVM_ON_WIN32)
#define HAVE_WRITEV 1
#endif

/* Define if /dev/zero should be used when mapping RWX memory, or undefine if
   its not necessary */
#undef NEED_DEV_ZERO_FOR_MMAP

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

#endif
