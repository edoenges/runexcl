// CPUCGroup.cpp
// Copyright (c) 2025-2025 Eric Doenges. All rights reserved.
// SPDX-License-Identifier: ZLib
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
#include "sysfs.hpp"

#include <fcntl.h>
#include <linux/sched.h> // Definition of struct clone_args
#include <sched.h>       // Definition of CLONE_* constants
#include <signal.h>
#include <sys/file.h>    // for flock(2)
#include <sys/inotify.h> // for inotify(7)
#include <sys/stat.h>
#include <sys/syscall.h> // Definition of SYS_* constants
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;

//
// Helper functions to interact with the filesystem
//

class FileLock
{
protected:
  int mFd;

public:
  FileLock(fs::path path, bool locked = false)
  {
    if (-1 == (mFd = ::open(path.string().c_str(), O_RDONLY)))
      throw std::system_error(errno, std::system_category(),
                              std::string("open(\"" + path.string() + "\")"));
    int err;
    do {
      if ((err = ::flock(mFd, LOCK_EX))) {
        if (EINTR != errno) {
          ::close(mFd);
          throw std::system_error(errno, std::system_category(), "flock");
        }
      }
    } while (err);
  }

  ~FileLock()
  {
    ::flock(mFd, LOCK_UN);
    ::close(mFd);
  }
};


// Remove '\n' and any following characters from the string.
std::string& chomp(std::string& str)
{
  auto end = str.find_first_of("\n");
  if (end != std::string::npos)
    str.resize(end);
  return str;
}

//! Write '+cpuset' to the cgroup.subtree_control file at path if it is not
//! already present.
//! \param path Path to the cgroup to modify.
static void enableCpusetController(char const* path)
{
  fs::path cgroup_subtree_control = fs::path(path) / "cgroup.subtree_control";
  std::string subtree_control     = sysfs_read(cgroup_subtree_control);
  if (std::string::npos == subtree_control.find("cpuset")) {
    sysfs_write(cgroup_subtree_control, "+cpuset");
  }
}

class INotify
{
protected:
  int mFd;

public:
  ~INotify()
  {
    ::close(mFd);
  }

  INotify()
  {
    if (-1 == (mFd = inotify_init1(IN_CLOEXEC)))
      throw std::system_error(errno, std::system_category(), "inotify_init1");
  }

  int add(char const* path, uint32_t mask)
  {
    int wd = inotify_add_watch(mFd, path, mask);
    if (-1 == wd)
      throw std::system_error(errno, std::system_category(),
                              "inotify_add_watch(\"" + std::string(path) +
                                  "\")");
    return wd;
  }
  int add(std::string const& path, uint32_t mask)
  {
    return add(path.c_str(), mask);
  }

  void remove(int wd)
  {
    if (inotify_rm_watch(mFd, wd))
      throw std::system_error(errno, std::system_category(),
                              "inotify_rm_watch");
  }

  void read_event(struct inotify_event* event)
  {
    // In typical Linux fashion, the desire to make everything look like a file
    // descriptor makes using the inotify API more complex than necessary. The
    // inotify_event structure is variable sized due to the name field at the
    // end. Since it is not possible to read partial events, we must either use
    // the FIONREAD ioctl to figure out many bytes are available for reading,
    // or use a buffer of sizeof(struct inotify_event) + NAME_MAX + 1 that can
    // hold at least one maximum-sized event. The poor design of this interface
    // continues with the fact that the read will return as many events as fit
    // into the buffer, with no way to tell the kernel that we want only a
    // single event. This means we're forced to do our own buffering if we want
    // to read only a single event at a time.
    // We can drastically simplify things for our usecase as we will never watch
    // directories, which means the event_notify structure will have a fixed
    // size.
    do {
      if (-1 == ::read(mFd, event, sizeof(*event))) {
        if (EINTR != errno)
          throw std::system_error(errno, std::system_category(),
                                  "read from inotify");
        continue;
      }
      assert(!event->len);
    } while (false);
  }
};

//
// Class functions to handle the runexcl.slice cgroup
//

// Make sure the runexcl.slice cgroup is setup and return a CPUSet object
// containing all the CPUs it can use.
CPUSet CPUCGroup::setupSlice()
{
  // Make sure the 'cpuset' controller is active for children of the root
  // cgroup.
  enableCpusetController(CGROUP_ROOT);

  // If the runexcl.slice cgroup doesn't exist, create it.
  if (::mkdir(RUNEXCL_SLICE,
              S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) {
    if (EEXIST != errno)
      throw std::system_error(errno, std::system_category(),
                              "mkdir(\"" RUNEXCL_SLICE "\")");
  }

  // Enable the 'cpuset' controller for the slice's children.
  enableCpusetController(RUNEXCL_SLICE);

  // Determine the effective cpuset for the slice.
  fs::path slice(RUNEXCL_SLICE);
  CPUSet   effective_set = CPUSet(sysfs_read(slice / "cpuset.cpus.effective"));

  // If the cpuset.cpus hasn't been set for the slice, set it to the effective
  // cpus. This is necessary because cgroup v2 will not let us create a remote
  // cpuset partition unless the parent's cpuset.cpus and cpuset.cpus.exclusive
  // are set.
  std::fstream file(slice / "cpuset.cpus");
  CPUSet       set;
  file >> set;
  if (set.empty())
    file << effective_set;

  return effective_set;
}

//
// Protected/private member functions
//

void CPUCGroup::remove()
{
  if (::rmdir(mPath.c_str()))
    throw std::system_error(errno, std::system_category(),
                            "rmdir(\"" + mPath + "\"):");
}

void CPUCGroup::set_partition_type(char const* type)
{
  std::fstream fpart(mPath + "/cpuset.cpus.partition");
  fpart << type;
  fpart.seekp(0);
  std::string _type;
  fpart >> _type;
  chomp(_type);
  if (type != _type)
    throw std::runtime_error("Could not set partition type to '" +
                             std::string(type) + "': " + _type);
}

//
// Public interface
//

CPUCGroup::~CPUCGroup()
{
  try {
    // Remove the cgroup.
    remove();

    // Remove the CPUs that where part of this group from
    // runexcl.slice/cpuset.cpus.exclusive to make them available to again.
    // Unfortunately, if a remote partition is removed, its CPUs are not
    // immediately available to the other partitions, and I found no way to
    // determine when the update is complete. As a result, we can't simply
    // remove any CPU that appears in runexcl.slice/cpuset.cpus.effective from
    // runexcl.slice/cpuset.cpus.exclusive, which would be the robust way to do
    // this. Instead, we assume mCPUSet is accurate.
    fs::path cpuset_cpus_exclusive =
        fs::path(RUNEXCL_SLICE) / "cpuset.cpus.exclusive";
    FileLock lock(cpuset_cpus_exclusive);
    CPUSet   exclusive = CPUSet(sysfs_read(cpuset_cpus_exclusive));

    // As an additional complication, CPUSet does not provide a NOT operation
    // (mainly because CPU_SET does not). As a result, we need to make use of
    // the fact that
    // (exclusive & ~mCPUSet) == (exclusive ^ mCPUSet) & exclusive
    //
    // Note that since we cannot write empty sets to files right now (see the
    // comment for UnixFile::operator<<(CPUSet const&) above), after the last
    // runexcl cgroup is removed, the slice's cpuset.cpus.exclusive will contain
    // a bogus value. This shouldn't matter, as the kernel will ignore this
    // value as long as there are no remote partitions, and CPUCGroup will not
    // use the cpuset.cpus.exclusive to check if the CPUs are available - it
    // will look in cpuset.cpus.effective instead.
    exclusive = (exclusive ^ mCPUSet) & exclusive;
    sysfs_write(cpuset_cpus_exclusive, exclusive);
  }
  catch (const std::exception& e) {
    // Destructors should throw exceptions, so just report the error.
    std::cerr << e.what() << '\n';
  }
}

CPUCGroup::CPUCGroup(CPUSet const& set) : mCPUSet(set)
{
  // Open runexcl.slice/cpuset.cpus.exclusive and lock it. We use this file to
  // keep track of which CPUs are already allocated. The lock is to prevent
  // race conditions when multiple runexcl processes try to allocate exclusive
  // CPUs.
  fs::path slice                 = fs::path(RUNEXCL_SLICE);
  fs::path cpuset_cpus_exclusive = slice / "cpuset.cpus.exclusive";
  FileLock lock(cpuset_cpus_exclusive);
  CPUSet   exclusive = CPUSet(sysfs_read(cpuset_cpus_exclusive));

  // Get the effective CPUs available to the slice.
  CPUSet available(sysfs_read(slice / "cpuset.cpus.effective"));

  if ((set & available) != set)
    throw std::runtime_error("Requested cpuset '" + set.to_string() +
                             "' not a subset of '" + available.to_string() +
                             "'");

  // Update the exclusive cpuset.
  exclusive |= set;
  sysfs_write(cpuset_cpus_exclusive, exclusive);

  // Use the cpuset to name the runexcl subslice..
  mPath = slice.string() + "/runexcl." + set.to_string();
  if (::mkdir(mPath.c_str(),
              S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) {
    throw std::system_error(errno, std::system_category(),
                            "mkdir(" + mPath + ")");
  }

  try {
    sysfs_write(fs::path(mPath) / "cpuset.cpus", set);
    set_partition_type("root");
  }
  catch (...) {
    remove();
    throw;
  }
}

void CPUCGroup::add(pid_t pid)
{
  sysfs_write(fs::path(mPath) / "cgroup.procs", std::to_string(pid));
}

pid_t CPUCGroup::clone(int flags)
{
  // Open an O_PATH file descriptor for the cgroup. We don't set O_CLOEXEC
  // because we'll close the file descriptor in both the parent and the child.
  int fd = ::open(mPath.c_str(), O_PATH);
  if (0 > fd)
    throw std::system_error(errno, std::system_category(),
                            "open(" + mPath + ")");

  struct clone_args args = {0};
  args.flags             = CLONE_INTO_CGROUP | flags;
  args.cgroup            = fd;
  args.exit_signal       = SIGCHLD;

  pid_t child = ::syscall(__NR_clone3, &args, sizeof(args));
  ::close(fd);
  if (-1 == child)
    throw std::system_error(errno, std::system_category(), "clone3() failed:");

  return child;
}

void CPUCGroup::wait_empty()
{
  std::ifstream fevents(mPath + "/cgroup.events");
  INotify       inotify;
  int           wd = inotify.add(mPath + "/cgroup.events", IN_MODIFY);

  while (true) {
    fevents.seekg(0);

    // operator>> will stop after the first space, so if we want to read an
    // entire line of input until the '\n', we have to use std::getline instead.
    std::string events;
    std::getline(fevents, events);

    auto pos = events.find("populated ");
    if (pos != std::string::npos) {
      events.erase(pos, ::strlen("populated "));
      chomp(events);
      if (!std::stoul(events))
        break;

      // In theory we should check that the watch descriptor in the event
      // matches with what inotify.add returned, but since we only have a
      // single watched file, this should not be necessary.
      struct inotify_event event = {0};
      inotify.read_event(&event);
      assert(event.wd == wd);
    }
    else {
      std::cerr << "unexpected input from " << mPath << std::endl; //XXX
      return;
      throw std::runtime_error("Unexpected input from '" + mPath +
                               "/cgroup.events'");
    }
  }
}