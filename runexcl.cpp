// runexcl.cpp
// Copyright (c) 2025-2025 Eric Doenges. All rights reserved.
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
// claim that you wrote the original software. If you use this software
// in a product, an acknowledgment in the product documentation would be
// appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
// misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// Eric Doenges
// Eric.Doenges@gmx.net
//

#include "CPUCGroup.hpp"
#include "CPUGovernor.hpp"
#include "CPUSet.hpp"

// Standard C++ headers
#include <iomanip>
#include <iostream>
#include <system_error>

// Standard C headers
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// GNU/Linux headers
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <sched.h> // Definition of CLONE_* constants
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct RunExclArgs
{
  CPUSet mSet;
  double mFrequency;
  bool   mIsolate;
} gArgs;

// Helper class to save and restore I/O stream format manipulators.
// Requires C++17 class template argument deduction (CTAD).
template<typename CharT, typename Traits> class ScopedIOSFormat
{
public:
  typedef std::basic_ios<CharT, Traits> basic_ios;

  explicit ScopedIOSFormat(basic_ios& stream)
    : mStream{stream}, mSaved{nullptr}
  {
    mSaved.copyfmt(stream);
  }

  ~ScopedIOSFormat()
  {
    mStream.copyfmt(mSaved);
  }

protected:
  basic_ios& mStream;
  basic_ios  mSaved;
};

void print_usage(std::ostream& out, int width, int base_indent, int tab_indent,
                 const char* usage)
{
  ScopedIOSFormat guard(out);

  int  pos = 0, first = 0, last = 0, indent = base_indent, columns = width;
  char c;
  while ((c = usage[pos])) {
    if (' ' == c) {
      last = pos;
    } // Possible linebreak at space character
    else if ('\t' == c) {
      int n = pos - first;
      out << std::setw(n + indent) << std::string(&usage[first], n);
      first = pos + 1;
      last  = first;
      columns -= (n + indent);
      indent = (n + base_indent) < tab_indent ? tab_indent - (n + indent) : 1;
    }
    else if ('\n' == c) {
      int n = pos - first + 1;
      out << std::setw(n + indent) << std::string(&usage[first], n);
      first   = pos + 1;
      indent  = base_indent;
      columns = width;
    } // Newline
    else if ((pos - first) >= (columns - indent)) {
      int n = last - first;
      out << std::setw(n + indent) << std::string(&usage[first], n)
          << std::endl;
      first   = last + 1;
      indent  = tab_indent;
      columns = width;
    } // Break the line

    pos += 1;
  }
}

void usage(int exit_code)
{
  std::cerr << "Usage: runexcl [OPTION]... COMMAND [PARAMS]...\n";
  print_usage(
      std::cerr, 79, 2, 28,
      "-c, --cpu-list <list>\tList of CPUs to use.\n"
      "-f, --frequency <freq>|max|min|nonlinear\tFrequency to set CPUs to.\n"
      "-i, --isolate\tIsolate selected CPUs.\n"
      "\n");
  exit(exit_code);
}

static struct option sLongOptions[] = {{"cpu-list", required_argument, nullptr,
                                        'c'},
                                       {"frequency", required_argument,
                                        nullptr, 'f'},
                                       {"isolate", no_argument, nullptr, 'i'},
                                       {"verbose", no_argument, nullptr, 'v'},
                                       {nullptr, 0, nullptr, 0}};

int main(int argc, char** argv)
{
  int c, index;
  // The '+' tells getopt_long not to rearange the options.
  while (-1 !=
         (c = getopt_long(argc, argv, "+c:f:iv", sLongOptions, &index))) {
    switch (c) {
    case 'c': // cpu
      try {
        CPUSet set(optarg);
        if (!set.empty())
          gArgs.mSet |= set;
      }
      catch (std::invalid_argument const& e) {
        std::cerr << "Invalid CPU specification: " << e.what() << std::endl;
        ::exit(1);
      }
      break;

    case 'f':
    {
      char* last;
      gArgs.mFrequency = strtod(optarg, &last);
      if (last == optarg) {
        if (!strcmp("max", last)) {
          gArgs.mFrequency = -1.0;
        }
        else if (!strcmp("min", last)) {
          gArgs.mFrequency = -2.0;
        }
        else if (!strcmp("nonlinear", last)) {
          gArgs.mFrequency = -3.0;
        }
        else {
          std::cerr << "Invalid CPU frequency argument" << std::endl;
          ::exit(1);
        }
      }
      else {
        if (gArgs.mFrequency <= 0.0) {
          std::cerr << "Invalid CPU frequency argument" << std::endl;
          ::exit(1);
        }
        else if (!strcmp("k", last) || !strcmp("kHz", last)) {
          gArgs.mFrequency *= 1000.0;
        }
        else if (!strcmp("M", last) || !strcmp("MHz", last)) {
          gArgs.mFrequency *= 1000000.0;
        }
        else if (!strcmp("G", last) || !strcmp("GHz", last)) {
          gArgs.mFrequency *= 1000000000.0;
        }
        else if (last[0] != '\0') {
          std::cerr << "Invalid CPU frequency argument - unknown unit"
                    << std::endl;
        }
      }
    } break;

    case 'i': // isolated
      gArgs.mIsolate = true;
      break;

    case 'v': // verbose
      break;

    case '?':
    default:
      // getopt_long should have output an error message.
      std::cout << std::endl;
      usage(1);
    }
  }

  // runexcl needs at least one non-option argument to use as the command to
  // run.
  if (optind >= argc)
    usage(1);

  // For now, a cpu set must be specified
  if (gArgs.mSet.empty())
    usage(1);

  // Get the pointer to the first of the command line to execute on the slice.
  char** run_argv = &argv[optind];

  // Block the SIGINT, SIGTERM, and SIGHUP signals. Note that we explicitly do
  // not block SIGQUIT so that you can stop runexcl without doing any cleanup
  // for debugging purposes (see also
  // https://www.gnu.org/software/libc/manual/html_node/Termination-Signals.html)
  sigset_t osignals, nsignals;
  sigemptyset(&nsignals);
  sigaddset(&nsignals, SIGINT);
  sigaddset(&nsignals, SIGTERM);
  sigaddset(&nsignals, SIGHUP);
  if (::sigprocmask(SIG_SETMASK, &nsignals, &osignals)) {
    std::cerr << "Setting signal mask failed: " << strerror(errno)
              << std::endl;
    return 1;
  }

  try {
    // Make sure runexcl.slice is set up and determine the set of CPUs available.
    CPUSet available = CPUCGroup::setupSlice();

    // Check if the requested CPUs are available
    CPUSet set = available & gArgs.mSet;
    if (set != gArgs.mSet) {
      std::cerr << "cpuset must be in '" << available.to_string() << "'."
                << std::endl;
      return 1;
    }

    CPUCGroup group(set);
    if (gArgs.mIsolate)
      group.isolate(true);

    CPUGovernor governor;
    if (gArgs.mFrequency != 0.0)
      governor.set_frequency(set, gArgs.mFrequency);

    // Clone the child process directly into the cgroup. Additionally add the
    // CLONE_VFORK flag to the clone system call because the parent process
    // doesn't need to run until the child process calls execve (actually, it
    // only needs to run after the child process terminates).
    pid_t child = group.clone(CLONE_VFORK);
    if (-1 == child) {
      throw std::system_error(errno, std::system_category(),
                              "clone3() failed:");
    }
    else if (!child) {
      try {
        // Set the main thread's CPU affinity mask.
        set.setaffinity();

        // Drop root privileges. Since runexcl will run as a SUID binary, it
        // should not be necessary to fiddle with the supplementary groups, as
        // those should be set correctly for the user running the binary - we
        // only need to drop the primary group in case the group S-bit is also
        // set on the binary.
        if (::setgid(getgid()) || ::setuid(getuid()))
          throw std::system_error(errno, std::system_category(),
                                  "Could not drop privileges:");

        // Close all open file descriptors except stdin, stdout, and stderr.
        // This is necessary because we cannot be sure all file descriptors
        // where opened with the FD_CLOEXEC flag. Note that we don't use
        // std::filesystem::directory_iterator here because getting at the
        // necessary information is more convenient using the POSIX APIs
        // directly.        
        DIR* dir = ::opendir("/proc/self/fd");
        if (dir) {
          int dfd = ::dirfd(dir);
          for (struct dirent* entry; entry = ::readdir(dir);) {
            // /proc/self/fd should only contain '.', '..', or names consisting
            // only of digits. strtol will return 0 for '.' and '..', so we
            // don't need to check for invalid characters in strtol.
            int fd = ::strtol(entry->d_name, nullptr, 10);
            if ((fd > 2) && (fd != dfd))
              ::close(fd);
          }
          ::closedir(dir);
        }

        /*
         * The child inherits the signal mask from the parent, so we restore
         * the signal mask the parent had before we blocked any signals above.
         */
        if (::sigprocmask(SIG_SETMASK, &osignals, NULL))
          throw std::system_error(errno, std::system_category(),
                                  "sigprocmask");

        if (execvp(run_argv[0], run_argv))
          throw std::system_error(errno, std::system_category(), run_argv[0]);
      }
      catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        // Call _exit because we don't want to call the cpugroup destructor in
        // the child.
        _exit(1);
      }
    } // Child process
    else {
      // Wait until the child terminates.
      int status;
      if (-1 == waitpid(child, &status, 0))
        throw std::system_error(errno, std::system_category(),
                                "waitpid() failed:");

      // And wait until the cgroup is empty. This is necessary in case the child
      // forked its own children that outlived it.
      group.wait_empty();
    } // Parent process
  }
  catch (std::system_error& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  return 0;
}