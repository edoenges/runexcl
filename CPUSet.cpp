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

#include "CPUSet.hpp"

#include <climits>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <system_error>

///
/// Helper function to get the maximum number of CPUs the kernel can support
/// so that we can size the CPU_SET correctly.
/// @return maximum number of CPUs the kernel supports
///
static int getMaxCPUs()
{
  // Store the maximum number of CPUs the kernel can manage, determined by
  // reading /sys/devices/system/cpu/kernel_max. Since this value should never
  // change, no thread synchronization is necessary.
  static int maxCPUs = 0;
  if (maxCPUs)
    return maxCPUs;

  // Set a default for maxCPUs if we cannot determine it from the system. We
  // use CPU_SETSIZE, which is defined to be 1024 according to the CPU_SET
  // manpage.
  maxCPUs = CPU_SETSIZE;

  // Get the maximum number of CPUs the kernel can handle from the sys
  // filesystem. If we cannot get the number, or it is smaller than CPU_SETSIZE,
  // use CPU_SETSIZE instead.
  FILE* fp = fopen("/sys/devices/system/cpu/kernel_max", "re");
  if (fp) {
    char buffer[128];
    if (fgets(buffer, sizeof(buffer), fp)) {
      char* end;
      long  n = strtol(buffer, &end, 10);
      if (('\n' == end[0]) || ('\0' == end[0])) {
        if (n > INT_MAX)
          n = INT_MAX;
        maxCPUs = (n > CPU_SETSIZE) ? n : CPU_SETSIZE;
      }
    }
    fclose(fp);
  }

  return maxCPUs;
}

CPUSet::CPUSet(bool init)
{
  mMaxCPUs = getMaxCPUs();
  mSize    = CPU_ALLOC_SIZE(mMaxCPUs);
  if (!(mSet = CPU_ALLOC(mMaxCPUs)))
    throw std::bad_alloc();
  if (init)
    zero();
}

CPUSet::CPUSet(CPUSet const& set) : mSize(set.mSize), mMaxCPUs(set.mMaxCPUs)
{
  if (!(mSet = CPU_ALLOC(mMaxCPUs)))
    throw std::bad_alloc();
  ::memcpy(mSet, set.mSet, mSize);
}

CPUSet::CPUSet(pid_t pid) : CPUSet()
{
  if (sched_getaffinity(pid, mSize, mSet))
    throw std::bad_alloc();
}

int CPUSet::first() const
{
  for (int i = 0; i < mMaxCPUs; ++i) {
    if (is_set(i))
      return i;
  }
  return -1;
}

int CPUSet::last() const
{
  for (int i = mMaxCPUs - 1; i >= 0; --i) {
    if (is_set(i))
      return i;
  }
  return -1;
}

void CPUSet::parse(char const* str)
{
  assert(str);

  // Clear the set before parsing.
  this->zero();

  int         start = -1;
  char const* pos   = str;
  while (true) {
    char*         end;
    unsigned long n = strtoul(pos, &end, 10);
    if (end == pos) {
      // No valid number at position. If this was at the very beginning of
      // the string, the cpuset is empty (which is valid).
      if (('\0' == *end) && (end == str))
        break;
      if (-1 == start)
        throw std::invalid_argument("Missing CPU number in cpuset string");
      else
        throw std::invalid_argument("Missing end of range in cpuset string");
    }

    if (n >= static_cast<unsigned long>(mMaxCPUs))
      throw std::range_error("CPU #" + std::to_string(n) +
                             " out of range in cpuset string");
    int cpu = static_cast<int>(n);

    switch (*end) {
    case '\0':
    case ',':
      if (-1 == start) {
        this->set(cpu);
      }
      else {
        if (start > cpu)
          throw std::out_of_range("Invalid CPU range " +
                                  std::to_string(start) + "-" +
                                  std::to_string(cpu) + " in cpuset string");
        while (start <= cpu)
          this->set(start++);
        start = -1;
      }
      break;

    case '-':
      if (-1 != start)
        throw std::invalid_argument("Invalid syntax in cpuset string");
      else
        start = cpu;
      break;

    default:
      throw std::invalid_argument("Invalid character '" +
                                  std::to_string(*end) + "' in cpuset string");
    }

    if ('\0' == *end)
      break;

    pos = ++end;
  }
}

std::string CPUSet::to_string() const
{
  std::string result;

  bool first = true;
  for (int n = 0; n < mMaxCPUs; ++n) {
    if (is_set(n)) {
      int start = n;
      // Find the first CPU after n that is not set.
      for (n += 1; n < mMaxCPUs; ++n) {
        if (!is_set(n))
          break;
      }

      if (first)
        first = false;
      else
        result.push_back(',');

      result += std::to_string(start);
      if (start != (n - 1)) {
        result.push_back('-');
        result += std::to_string(n - 1);
      }
    }
  }

  return result;
}

// Note that just like the standard library, we do not attempt to reset the
// input position in the stream in case parsing of the CPUSet fails.
// \note operator>> does not do exactly the same thing as parse() because
// parse() can throw various exceptions, while operator>> will just set failbit
// for any parse error.
std::istream& operator>>(std::istream& in, CPUSet& set)
{
  CPUSet _set;

  std::istream::sentry sentry(in);
  if (sentry) {
    int start = -1;
    while (in.good()) {
      // Unfortunately, operator>>(unsigned&) does not treat negative numbers
      // as errors, so we have to input a signed number and check for negative
      // numbers ourselves.
      int cpu;
      in >> cpu;
      if (in.fail()) {
        break;
      }
      else if ((cpu < 0) || (cpu >= _set.mMaxCPUs)) {
        in.setstate(std::ios_base::failbit);
        break;
      }

      // Check if there is a character following the number.
      std::istream::int_type c;
      if (in.eof()) {
        c = std::istream::traits_type::eof();
      }
      else {
        c = in.get();
        if (in.fail())
          break;
      }

      if ('-' == c) {
        // Range expression
        if (-1 != start)
          in.setstate(std::ios_base::failbit);
        else
          start = cpu;
      }
      else {
        if (-1 == start) {
          _set.set(cpu);
        }
        else {
          if (start > cpu) {
            in.setstate(std::ios_base::failbit);
          }
          else {
            while (start <= cpu)
              _set.set(start++);
            start = -1;
          }
        }

        // Any character except ',' indicates the end of the set. Note that
        // we don't want to unget EOF, so we ignore it here and rely on the
        // while (in.good()) above to terminate the loop.
        if ((',' != c) && (std::istream::traits_type::eof() != c)) {
          in.unget();
          goto done;
        }
      }
    }
  }
  else {
    // If the sentry object sets the eof() bit, end-of-file was reached before
    // any input was read. We interpret this as a (valid !) empty CPUSet and
    // clear all state bits except eofbit.
    if (in.eof())
      in.clear(std::ios_base::eofbit);
  }

done:
  if (!in.fail())
    set = std::move(_set);

  return in;
}

std::ostream& operator<<(std::ostream& out, CPUSet const& set)
{
  // \todo: implement without to_string(), then remove to_string (or implement
  // to_string using operator<< instead).
  std::string str = set.to_string();
  out << str;
  return out;
}
