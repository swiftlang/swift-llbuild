#define _GNU_SOURCE 1
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "llvm/Support/CommandLine.h"

#if defined(_WIN32)
  #include <time.h>
  #include <sys/utime.h>
  #define UTIME_NOW ((1l << 30) - 1l)
  #define UTIME_OMIT ((1l << 30) - 2l)
#else
#if defined(__OpenBSD__)
  #include <sys/types.h>
  #include <utime.h>
#endif
  #include <sys/time.h>
#endif

#if __has_include(<sysexit.h>)
#include <sysexists.h>
#else
#define EX_USAGE 64
#define EX_DATAERR 65
#define EX_NOINPUT 66
#endif

/// Set the file time access and modification times to a specified time,
/// or to the current time.
static int file_time_set_fixed(const char* filename, struct timespec time_to_set) {
#if defined __APPLE__
  if (__builtin_available(macOS 10.13, iOS 11.0, watchOS 4.0, tvOS 11.0, *)) {
    struct timespec times[2] = { time_to_set, time_to_set };
    return utimensat(AT_FDCWD, filename, times, 0);
  } else {
    // If requested to reset atime/mtime to "now", use the shortcut
    // instead of computing and using the current time.
    if (time_to_set.tv_nsec == UTIME_NOW) {
      return utimes(filename, NULL);
    }
    struct timeval tv;
    tv.tv_sec = time_to_set.tv_sec;
    tv.tv_usec = (__darwin_suseconds_t)(time_to_set.tv_nsec / 1000);
    struct timeval times[2] = { tv, tv };
    return utimes(filename, times);
  }
#elif defined __linux__
  struct timespec times[2] = { time_to_set, time_to_set };
  return utimensat(AT_FDCWD, filename, times, 0);
#else
  // If requested to reset atime/mtime to "now", use the shortcut
  // instead of computing and using the current time.
  if (time_to_set.tv_nsec == UTIME_NOW) {
    return utime(filename, NULL);
  }
  struct utimbuf times = { time_to_set.tv_sec, time_to_set.tv_sec };
  return utime(filename, &times);
#endif
}

static struct timespec stat_mtime_as_timespec(const struct stat* sb) {
#if defined(__APPLE__)
  return sb->st_mtimespec;
#elif defined(_WIN32)
  return { sb->st_mtime, 0 };
#else
  return sb->st_mtim;
#endif
}

/// Get the high resolution modification time for the filename.
static int get_file_mtime(const char* filename, struct timespec* ts) {
  struct stat sb;
  if (stat(filename, &sb) == -1) {
    return -1;
  }
  *ts = stat_mtime_as_timespec(&sb);
  return 0;
}

/// Adjust the file time access and modification times
/// relative to existing atime/mtime.
static int file_time_adjust_relative(const char* filename, double relative_sec) {

  struct timespec ts;
  if (get_file_mtime(filename, &ts) == -1) {
    return -1;
  }

  ts.tv_sec += trunc(relative_sec);
  ts.tv_nsec += 1000000000 * (relative_sec - trunc(relative_sec));
  if (ts.tv_nsec < 0) {
    ts.tv_sec -= 1;
    ts.tv_nsec += 1000000000;
  } else if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000;
  }

  return file_time_set_fixed(filename, ts);
}

static int file_time_adjust_epsilon(const char* filename, int direction) {
  struct timespec previous_timestamp;
  if (get_file_mtime(filename, &previous_timestamp) == -1) {
    return -1;
  }

  // Attempt differently sized ULPs for different filesystems.
  // What do we know about the file systems' modify time resolutions:
  //  FAT: 2 seconds
  //  HFS, ext3: 1 second
  //  ext4: 1 millisecond
  //  NTFS: 100 nanoseconds
  //  APFS: nanosecond
  static double epsilons[] = {2, 1, 0.001, 0.000001, 0.0000001, 0.000000001};

  // Try to adjust several times with increasingly growing epsilon
  // and see if adjustments results in a material change.
  for (int i = sizeof(epsilons) / sizeof(epsilons[0]) - 1; i >= 0; i--) {
    double e = epsilons[i] * ((direction < 0) ? -1 : 1);
    if (file_time_adjust_relative(filename, e)) {
      return -1;
    }
    struct timespec new_timestamp;
    if (get_file_mtime(filename, &new_timestamp) == -1) {
      return -1;
    }
    if (new_timestamp.tv_sec != previous_timestamp.tv_sec
     || new_timestamp.tv_nsec != previous_timestamp.tv_nsec) {
      return 0;
    }
  }

  fprintf(stderr, "%s: Can't adjust time to %+.9g\n", filename, epsilons[0]);
  errno = ERANGE;
  return -1;
}


/// Format as a local time plus fractions of a second.
static void time_format(char *buf, size_t size, struct timespec ts) {
    time_t clock = ts.tv_sec;
    struct tm* tm = localtime(&clock);
    if (tm == 0) {
      snprintf(buf, size, "<can't find local time>");
      return;
    }

    char time_format[24];
    int ret = snprintf(time_format, sizeof(time_format), "%%F %%T.%09ld", ts.tv_nsec);
    if (ret <= 0 || ret >= (int)sizeof(time_format)) {
      snprintf(buf, size, "<can't produce local time format>");
      return;
    }

    if (strftime(buf, size, time_format, tm) == 0) {
      snprintf(buf, size, "<can't format local time>");
      return;
    }
}

int main(int argc, char **argv) {
  using namespace llvm;

  cl::opt<int>         Adjust   ("adjust", cl::init(0), cl::desc("Move access and modification times"));
  cl::alias            AdjustA  ("a", cl::aliasopt(Adjust));
  cl::opt<bool>        MTimeA   ("mtime-add-ulp", cl::desc("Move the a/mtime a smallest amount backwards"));
  cl::opt<bool>        MTimeM   ("mtime-minus-ulp", cl::desc("Move the a/mtime a smallest amount Forwards"));
  cl::opt<bool>        NowP     ("now-plus-ulp", cl::desc("Set to current time plus epsilon"));
  cl::opt<bool>        NowM     ("now-minus-ulp", cl::desc("Set to current time minus epsilon"));
  cl::opt<std::string> Filename ("from-file", cl::init(""), cl::desc("Set access and modification times from FILE"), cl::value_desc("file"));
  cl::alias            FilenameA("f", cl::aliasopt(Filename));
  cl::opt<bool>        Verbose  ("verbose", cl::desc("Show what is being done"));
  cl::alias            VerboseA ("v", cl::aliasopt(Verbose));
  cl::list<std::string> InputFilenames(cl::Positional, cl::desc("<Input files>"), cl::OneOrMore);

  cl::ParseCommandLineOptions(argc, argv, "Modify timestamps of existing files\n");

  enum mode {
    ModeSetToNow,
    ModeSetToFile,      // -f, --from-file
    ModeAdjustMtime,    // -a, --adjust
    ModeMtimeMinusULP,  // --mtime-minus-ulp
    ModeMtimePlusULP,   // --mtime-plus-ulp
    ModeNowMinusULP,    // --now-minus-ulp
    ModeNowPlusULP,     // --now-plus-ulp
  } mode = ModeSetToNow;

  struct timespec time_to_set = { 0, UTIME_NOW };
  double adjust_relative_sec = 0;

  if (MTimeM) {
    mode = ModeMtimeMinusULP;
  }
  if (MTimeA) {
    mode = ModeMtimePlusULP;
  }
  if (NowP) {
    mode = ModeNowPlusULP;
  }
  if (NowM) {
    mode = ModeNowMinusULP;
  }
  if (Adjust) {
    double value = Adjust;
    if (value < -1000000 || value > 1000000) {
      errno = ERANGE;
    }
    if (errno != 0) {
      std::cerr << "--adjust " << Adjust << ": " << strerror(errno) << std::endl;
      exit(EX_DATAERR);
    }
    adjust_relative_sec = value;
    mode = ModeAdjustMtime;
  }
  if (!Filename.empty()) {
    if (get_file_mtime(Filename.c_str(), &time_to_set) == -1) {
      std::cerr << "--from-file " << Filename << ": " << strerror(errno) << std::endl;
      exit(EX_DATAERR);
    }
    mode = ModeSetToFile;
  }

  // A list of filenames is required.
  if (InputFilenames.empty()) {
    cl::PrintHelpMessage();
    exit(EX_USAGE);
  }

  // Modify all files' access and modification times.
  for (std::string file : InputFilenames) {
    const char* filename = file.c_str();

    struct timespec previous_timestamp = { 0, UTIME_OMIT };
    if (Verbose) {
      if (get_file_mtime(filename, &previous_timestamp) == -1) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(EX_NOINPUT);
      }
    }

    switch (mode) {
    case ModeSetToNow:
    case ModeSetToFile:   // -f, --from
      if (file_time_set_fixed(filename, time_to_set) == -1) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(EX_NOINPUT);
      }
      break;
    case ModeAdjustMtime:  // -a, --adjust
      if (file_time_adjust_relative(filename, adjust_relative_sec) == -1) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(EX_NOINPUT);
      }
      break;
    case ModeMtimePlusULP:
    case ModeMtimeMinusULP:
      if (file_time_adjust_epsilon(filename, mode == ModeMtimeMinusULP ? -1 : 1) == -1) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(EX_NOINPUT);
      }
      break;
    case ModeNowPlusULP:
    case ModeNowMinusULP:
      struct timespec now = { 0, UTIME_NOW };
      if (file_time_set_fixed(filename, now) == -1) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(EX_NOINPUT);
      }
      if (file_time_adjust_epsilon(filename, mode == ModeNowMinusULP ? -1 : 1) == -1) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(EX_NOINPUT);
      }
      break;
    }

    if (Verbose) {
      struct timespec final_timestamp;
      if (get_file_mtime(filename, &final_timestamp) == -1) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(EX_NOINPUT);
      }

      fprintf(stderr, "%s: ", filename);
      switch (mode) {
      case ModeSetToNow:
      case ModeSetToFile:
        fprintf(stderr, "reset");
        break;
      case ModeAdjustMtime:
        fprintf(stderr, "adjusted %+.9gs", adjust_relative_sec);
        break;
      case ModeMtimeMinusULP:
        fprintf(stderr, "adjusted backward");
        break;
      case ModeMtimePlusULP:
        fprintf(stderr, "adjusted forward");
        break;
      case ModeNowMinusULP:
        fprintf(stderr, "set to now - epsilon");
        break;
      case ModeNowPlusULP:
        fprintf(stderr, "set to now + epsilon");
        break;
      }

      char buf_prev[64];
      char buf_final[64];
      time_format(buf_prev, sizeof(buf_prev), previous_timestamp);
      time_format(buf_final, sizeof(buf_final), final_timestamp);

      fprintf(stderr, "\n  from %s\n  to   %s\n", buf_prev, buf_final);
    }
  }

  return 0;
}
