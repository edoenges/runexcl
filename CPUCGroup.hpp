// CPUCGroup.hpp
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
#ifndef CPUCGroup_hpp
#define CPUCGroup_hpp

#include "CPUSet.hpp"

#include <string>
#include <unistd.h>

//! Path to cgroup root
#define CGROUP_ROOT "/sys/fs/cgroup"

//! Path to runexcl.slice
#define RUNEXCL_SLICE CGROUP_ROOT "/runexcl.slice"

class CPUCGroup
{
protected:
  CPUSet      mCPUSet;
  std::string mPath;

  //! Remove the cgroup from the filesystem.
  void remove();
  //! Set the cpuset partition type.
  void set_partition_type(char const* type);

public:
  ~CPUCGroup();
  CPUCGroup(CPUSet const& set);

  void add(pid_t pid);

  void isolate(bool enable = true)
  {
    set_partition_type(enable ? "isolated" : "root");
  }

  // Clone a child process into the cgroup using the clone3 system call.
  // The flags parameter can be used to add additional flags to the clone3
  // system call. The following flags can be added: CLONE_CLEAR_SIGHAND,
  // CLONE_FILES, CLONE_FS, CLONE_IO, CLONE_NEWCGROUP, CLONE_NEWIPC,
  // CLONE_NEWNET, CLONE_NEWNS, CLONE_NEWPID,  CLONE_NEWUSER, CLONE_NEWUTS,
  // CLONE_PTRACE, CLONE_UNTRACED, and CLONE_VFORK. The flags parameter is not
  // checked for validity.
  pid_t clone(int flags = 0);

  void wait_empty();

  // runexcl.slice management
  static CPUSet setupSlice();
};

#endif // CPUCGroup_hpp
