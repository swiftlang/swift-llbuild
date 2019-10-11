#define _GNU_SOURCE 1
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <sysexits.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <string.h>

/// Show adjust-mtime help screen.
static void usage(const char* progname, bool help_screen = false) {
  fprintf(help_screen ? stdout : stderr,
    "Modify timestamps of existing files\n"
    "Usage: %s [OPTIONS] <filename> ...\n"
    "Where OPTIONS are:\n"
    " -a, --adjust=<[-]seconds>  Move access and modification times\n"
    "     --mtime-add-ulp        Move the a/mtime a smallest amount backward\n"
    "     --mtime-minus-ulp      Move the a/mtime a smallest amount forward\n"
    "     --now-plus-ulp         Set to current time plus epsilon\n"
    "     --now-minus-ulp        Set to current time minus epsilon\n"
    " -f, --from-file=<FILE>     Set access and modification times from FILE\n"
    " -v, --verbose              Show what is being done\n"
    " -h, --help                 Show this screen\n"
    , progname);
}

/// Get the current wall clock time.
struct timespec get_current_time() {
  struct timespec now;
#if defined(_WIN32)
  struct timeval tv;
  if (gettimeofday(&tv, 0) == -1) {
    fprintf(stderr, "gettimeofday(): %s\n", strerror(errno));
    exit(EX_OSERR);
  }
  now.tv_sec = tv.tv_sec;
  now.tv_nsec = tv.tv_usec * 1000;
#else
  if (clock_gettime(CLOCK_REALTIME, &now) == -1) {
    fprintf(stderr, "clock_gettime(): %s\n", strerror(errno));
    exit(EX_OSERR);
  }
#endif
  return now;
}

/// Set the file time access and modification times to a specified time,
/// or to the current time.
int file_time_set_fixed(const char* filename, struct timespec time_to_set) {
#if defined(_WIN32)
  // If requested to reset atime/mtime to "now", use the shortcut
  // instead of computing and using the current time.
  if (time_to_set.tv_nsec == UTIME_NOW) {
    return utime(filename, NULL);
  }
  struct utimbuf times = { time_to_set.tv_sec, time_to_set.tv_sec };
  return utime(filename, &times);
#else
  struct timespec times[2] = { time_to_set, time_to_set };
  return utimensat(AT_FDCWD, filename, times, 0);
#endif
}

static struct timespec stat_mtime_as_timespec(const struct stat* sb) {
#if defined(__APPLE__)
  return { sb->st_mtimespec.tv_sec, sb->st_mtimespec.tv_nsec };
#elif defined(_WIN32)
  return { sb->st_mtime, 0 };
#else
  return { sb->st_mtim.tv_sec, sb->st_mtim.tv_usec * 1000 };
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
  const char* progname = basename(argv[0]);
  static struct option cli_options[] = {
    { "adjust",    required_argument, 0, 'a' },
    { "mtime-minus-ulp", no_argument, 0, 'M' },
    { "mtime-plus-ulp",  no_argument, 0, 'P' },
    { "now-minus-ulp", no_argument,   0, 'm' },
    { "now-plus-ulp",   no_argument,  0, 'p' },
    { "from-file", required_argument, 0, 'f' },
    { "verbose",   no_argument,       0, 'v' },
    { "help",      no_argument,       0, 'h' },
    { 0, 0, 0, 0 }
  };

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
  bool verbose = false;

  for (;;) {
    int c = getopt_long(argc, argv, "a:f:vh", cli_options, 0);

    switch (c) {
    case 'M':
      mode = ModeMtimeMinusULP;
      continue;
    case 'P':
      mode = ModeMtimePlusULP;
      continue;
    case 'm':
      mode = ModeNowMinusULP;
      continue;
    case 'p':
      mode = ModeNowPlusULP;
      continue;
    case 'a':
      {
        char *end = NULL;
        errno = 0;
        double value = strtod(optarg, &end);
        if (!isfinite(value) || value < -1000000 || value > 1000000 || !end || *end != '\0') {
          errno = ERANGE;
        }
        if (errno != 0) {
          fprintf(stderr, "--adjust %s: %s\n", optarg, strerror(errno));
          exit(EX_DATAERR);
        }
        adjust_relative_sec = value;
        mode = ModeAdjustMtime;
      }
      continue;
    case 'f':
      {
        if (get_file_mtime(optarg, &time_to_set) == -1) {
          fprintf(stderr, "--from-file %s: %s\n", optarg, strerror(errno));
          exit(EX_DATAERR);
        }
        mode = ModeSetToFile;
      }
      continue;
    case 'v':
      verbose = true;
      continue;
    case 'h':
      usage(progname, true);
      exit(EX_OK);
    case -1:
      break;
    case ':':
    case '?':
      exit(EX_USAGE);
    default:
      usage(progname);
      exit(EX_USAGE);
    }
    break;
  }

  // A list of filenames is required.
  if (optind == argc) {
    usage(progname);
    exit(EX_USAGE);
  }

  // Modify all files' access and modification times.
  for (; optind < argc; optind++) {
    const char* filename = argv[optind];

    struct timespec previous_timestamp = { 0, UTIME_OMIT };
    if (verbose) {
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

    if (verbose) {
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
