// CPUSet_tests.cpp
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

#include "CPUSet.hpp"

#include "gtest/gtest.h"

#include <sstream>

class MemFile
{
protected:
  FILE* mFp;
  char* mBuffer;
  bool  mIsBufferOwner;

public:
  MemFile(char* buffer, size_t size, char const* mode)
    : mBuffer(buffer), mIsBufferOwner(false)
  {
    if (!(mFp = ::fmemopen(mBuffer, size, mode)))
      throw std::system_error(errno, std::system_category(), "fmemopen");
  }

  MemFile(size_t size, char const* mode) : mIsBufferOwner(true)
  {
    mBuffer = new char[size];
    if (!(mFp = ::fmemopen(mBuffer, size, mode)))
      throw std::system_error(errno, std::system_category(), "fmemopen");
  }

  ~MemFile()
  {
    if (mFp)
      ::fclose(mFp);
    if (mIsBufferOwner)
      delete[] mBuffer;
  }

  operator FILE*() const
  {
    return mFp;
  }

  operator char const*()
  {
    return mBuffer;
  }

  void flush()
  {
    if (::fflush(mFp))
      throw std::system_error(errno, std::system_category(), "fflush");
  }
};

TEST(CPUSetCase, parse)
{
  CPUSet set;

  ASSERT_NO_THROW(set.parse(""));
  EXPECT_EQ(set.count(), 0);

  EXPECT_THROW(set.parse("-1"), std::range_error);
  EXPECT_THROW(set.parse(","), std::invalid_argument);
  EXPECT_THROW(set.parse("0,"), std::invalid_argument);
  EXPECT_THROW(set.parse("0-"), std::invalid_argument);
  EXPECT_THROW(set.parse("0-,1"), std::invalid_argument);
  EXPECT_THROW(set.parse("0-1,"), std::invalid_argument);
  EXPECT_THROW(set.parse("0-1-"), std::invalid_argument);
  EXPECT_THROW(set.parse("1-0"), std::out_of_range);

  ASSERT_NO_THROW(set.parse("0-2,4,6-7,9"));
  EXPECT_EQ(set.count(), 7);
  EXPECT_TRUE(set.is_set(0));
  EXPECT_TRUE(set.is_set(1));
  EXPECT_TRUE(set.is_set(2));
  EXPECT_FALSE(set.is_set(3));
  EXPECT_TRUE(set.is_set(4));
  EXPECT_FALSE(set.is_set(5));
  EXPECT_TRUE(set.is_set(6));
  EXPECT_TRUE(set.is_set(7));
  EXPECT_FALSE(set.is_set(8));
  EXPECT_TRUE(set.is_set(9));

  char string[128];
  snprintf(string, sizeof(string), "%d", set.max_cpus());
  EXPECT_THROW(set.parse(string), std::range_error);
}

TEST(CPUSetCase, to_string)
{
  CPUSet set;
  char   expected[128];

  set.set(0);
  EXPECT_STREQ(set.to_string().c_str(), "0");

  set.zero();
  set.set(1);
  EXPECT_STREQ(set.to_string().c_str(), "1");

  set.zero();
  set.set(set.max_cpus() - 1);
  snprintf(expected, sizeof(expected), "%d", set.max_cpus() - 1);
  EXPECT_STREQ(set.to_string().c_str(), expected);

  set.zero();
  set.set(0);
  set.set(2);
  set.set(3);
  set.set(set.max_cpus() - 1);
  snprintf(expected, sizeof(expected), "0,2-3,%d", set.max_cpus() - 1);
  EXPECT_STREQ(set.to_string().c_str(), expected);

  set.zero();
  set.set(0);
  set.set(2);
  set.set(3);
  set.set(set.max_cpus() - 2);
  set.set(set.max_cpus() - 1);
  snprintf(expected, sizeof(expected), "0,2-3,%d-%d", set.max_cpus() - 2,
           set.max_cpus() - 1);
  EXPECT_STREQ(set.to_string().c_str(), expected);
}

TEST(CPUSetCase, operator_in)
{
  CPUSet             set;
  std::istringstream ss;

  ss.str("");
  ss >> set;
  EXPECT_FALSE(ss.bad());
  EXPECT_FALSE(ss.fail());
  EXPECT_EQ(0, set.count());

  ss.clear();
  ss.str("-1");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str(",");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str("0,");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str("0-");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str("0-,1");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str("0-1,");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str("0-1-");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str("1-0");
  ss >> set;
  EXPECT_TRUE(ss.fail());

  ss.clear();
  ss.str("0-2,4,6-7,9");
  ss >> set;
  EXPECT_FALSE(ss.bad());
  EXPECT_FALSE(ss.fail());
  EXPECT_EQ(7, set.count());
  EXPECT_TRUE(set.is_set(0));
  EXPECT_TRUE(set.is_set(1));
  EXPECT_TRUE(set.is_set(2));
  EXPECT_FALSE(set.is_set(3));
  EXPECT_TRUE(set.is_set(4));
  EXPECT_FALSE(set.is_set(5));
  EXPECT_TRUE(set.is_set(6));
  EXPECT_TRUE(set.is_set(7));
  EXPECT_FALSE(set.is_set(8));
  EXPECT_TRUE(set.is_set(9));

  ss.clear();
  ss.str("0-2,4,6-7,9\nGarbage");
  ss >> set;
  EXPECT_FALSE(ss.bad());
  EXPECT_FALSE(ss.fail());
  EXPECT_EQ(7, set.count());
  EXPECT_TRUE(set.is_set(0));
  EXPECT_TRUE(set.is_set(1));
  EXPECT_TRUE(set.is_set(2));
  EXPECT_FALSE(set.is_set(3));
  EXPECT_TRUE(set.is_set(4));
  EXPECT_FALSE(set.is_set(5));
  EXPECT_TRUE(set.is_set(6));
  EXPECT_TRUE(set.is_set(7));
  EXPECT_FALSE(set.is_set(8));
  EXPECT_TRUE(set.is_set(9));

  char string[128];
  snprintf(string, sizeof(string), "%d", set.max_cpus());
  ss.clear();
  ss.str(string);
  ss >> set;
  EXPECT_TRUE(ss.fail());
}

TEST(CPUSetCase, operator_out)
{
  CPUSet             set;
  std::ostringstream ss;
  char               expected[128];

  set.set(0);
  ss.clear();
  ss.str("");
  ss << set;
  EXPECT_STREQ("0", ss.str().c_str());

  set.zero();
  set.set(1);
  ss.clear();
  ss.str("");
  ss << set;
  EXPECT_STREQ("1", ss.str().c_str());

  set.zero();
  set.set(set.max_cpus() - 1);
  ss.clear();
  ss.str("");
  ss << set;
  snprintf(expected, sizeof(expected), "%d", set.max_cpus() - 1);
  EXPECT_STREQ(expected, ss.str().c_str());

  set.zero();
  set.set(0);
  set.set(2);
  set.set(3);
  set.set(set.max_cpus() - 1);
  ss.clear();
  ss.str("");
  ss << set;
  snprintf(expected, sizeof(expected), "0,2-3,%d", set.max_cpus() - 1);
  EXPECT_STREQ(expected, ss.str().c_str());

  set.zero();
  set.set(0);
  set.set(2);
  set.set(3);
  set.set(set.max_cpus() - 2);
  set.set(set.max_cpus() - 1);
  ss.clear();
  ss.str("");
  ss << set;
  snprintf(expected, sizeof(expected), "0,2-3,%d-%d", set.max_cpus() - 2,
           set.max_cpus() - 1);
  EXPECT_STREQ(expected, ss.str().c_str());
}