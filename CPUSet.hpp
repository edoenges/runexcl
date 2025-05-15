// CPUSet.cpp
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
#ifndef CPUSet_h
#define CPUSet_h

#include <sched.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>

// Wrapper class around CPU_SET
class CPUSet
{
protected:
  cpu_set_t* mSet;
  size_t     mSize;
  int        mMaxCPUs;

public:
  ~CPUSet()
  {
    CPU_FREE(mSet);
  }

  CPUSet(bool init = true);
  CPUSet(CPUSet const& set);
  CPUSet(CPUSet&& set) : mSize(set.mSize), mMaxCPUs(set.mMaxCPUs)
  {
    mSet         = set.mSet;
    set.mSet     = nullptr;
    set.mSize    = 0;
    set.mMaxCPUs = 0;
  }
  CPUSet(char const* str) : CPUSet()
  {
    parse(str);
  }
  CPUSet(std::string str) : CPUSet(str.c_str()) {}
  CPUSet(pid_t pid);

  CPUSet& operator=(CPUSet const& rhs)
  {
    assert(mSize = rhs.mSize);
    ::memcpy(mSet, rhs.mSet, mSize);
    return *this;
  }

  CPUSet& operator=(CPUSet&& rhs)
  {
    CPU_FREE(mSet);
    mSet         = rhs.mSet;
    mSize        = rhs.mSize;
    mMaxCPUs     = rhs.mMaxCPUs;
    rhs.mSet     = nullptr;
    rhs.mSize    = 0;
    rhs.mMaxCPUs = 0;
    return *this;
  }

  CPUSet& operator=(char const* str)
  {
    parse(str);
    return *this;
  }

  operator cpu_set_t*()
  {
    return mSet;
  }

  int max_cpus() const
  {
    return mMaxCPUs;
  }

  void zero()
  {
    CPU_ZERO_S(mSize, mSet);
  }

  void set(int n)
  {
    assert(n >= 0 && n < mMaxCPUs);
    CPU_SET_S(n, mSize, mSet);
  }

  void clr(int n)
  {
    assert(n >= 0 && n < mMaxCPUs);
    CPU_CLR_S(n, mSize, mSet);
  }

  bool is_set(int n) const
  {
    assert(n >= 0 && n < mMaxCPUs);
    return CPU_ISSET_S(n, mSize, mSet);
  }

  int count() const
  {
    return CPU_COUNT_S(mSize, mSet);
  }

  bool empty() const
  {
    return 0 == count();
  }

  int first() const;
  int last() const;

  CPUSet& operator&=(CPUSet const& rhs)
  {
    assert(mSize == rhs.mSize);
    CPU_AND_S(mSize, mSet, mSet, rhs.mSet);
    return *this;
  }

  friend CPUSet operator&(CPUSet const& lhs, CPUSet const& rhs)
  {
    CPUSet result(false);
    assert((result.mSize == lhs.mSize) && (result.mSize == rhs.mSize));
    CPU_AND_S(result.mSize, result.mSet, lhs.mSet, rhs.mSet);
    return result;
  }

  CPUSet& operator|=(CPUSet const& rhs)
  {
    assert(mSize == rhs.mSize);
    CPU_OR_S(mSize, mSet, mSet, rhs.mSet);
    return *this;
  }

  friend CPUSet operator|(CPUSet const& lhs, CPUSet const& rhs)
  {
    CPUSet result(false);
    assert((result.mSize == lhs.mSize) && (result.mSize == rhs.mSize));
    CPU_OR_S(result.mSize, result.mSet, lhs.mSet, rhs.mSet);
    return result;
  }

  CPUSet& operator^=(CPUSet const& rhs)
  {
    assert(mSize == rhs.mSize);
    CPU_XOR_S(mSize, mSet, mSet, rhs.mSet);
    return *this;
  }

  friend CPUSet operator^(CPUSet const& lhs, CPUSet const& rhs)
  {
    CPUSet result(false);
    assert((result.mSize == lhs.mSize) && (result.mSize == rhs.mSize));
    CPU_XOR_S(result.mSize, result.mSet, lhs.mSet, rhs.mSet);
    return result;
  }

  bool operator==(CPUSet const& rhs) const
  {
    // Currently, there is no way to create CPUSet instances of different
    // sizes, so there is no need to check if both sets have the same size.
    assert(mSize == rhs.mSize);
    return CPU_EQUAL_S(mSize, mSet, rhs.mSet) ? true : false;
  }

  bool operator!=(CPUSet const& rhs) const
  {
    return !(*this == rhs);
  }

  void parse(char const* str);
  void parse(std::string const& str)
  {
    parse(str.c_str());
  }

  std::string to_string() const;

  void getaffinity(pid_t pid = 0)
  {
    if (sched_getaffinity(pid, mSize, mSet))
      throw std::system_error(errno, std::system_category(),
                              "sched_getaffinity");
  }

  void setaffinity(pid_t pid = 0)
  {
    if (sched_setaffinity(pid, mSize, mSet))
      throw std::system_error(errno, std::system_category(),
                              "sched_setaffinity");
  }

  friend std::istream& operator>>(std::istream&, CPUSet&);
  friend std::ostream& operator<<(std::ostream&, CPUSet const&);
};

#endif // CPUSet_h