// CPUGovernor.cpp
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

// See the following links on more information about CPU frequency control for
// Linux:
// https://www.kernel.org/doc/Documentation/cpu-freq/user-guide.txt
// https://docs.kernel.org/admin-guide/pm/amd-pstate.html
// https://wiki.archlinux.org/title/CPU_frequency_scaling
//
// With Kernel 6.11, apparently the way to go is:
//
// Put amd_pstate in 'passive' mode:
//   echo "passive" >/sys/devices/system/cpu/amd_pstate/status
//
// Put the CPU's governor in 'userspace' mode:
//   echo "userspace" >/sys/devices/system/cpu/cpufreq/policy<N>/scaling_governor
//
// Set the desired CPU frequency:
//
//   echo 20000000 >/sys/devices/system/cpu/cpufreq/policy<N>/scaling_setspeed
//

#include "CPUGovernor.hpp"
#include "sysfs.hpp"

#include <new>
#include <vector>


//! Path to cpu root
#define CPU_ROOT "/sys/devices/system/cpu"

//! Path to cpufreq root
#define CPUFREQ_ROOT CPU_ROOT "/cpufreq"

//! Path to AMD amd_pstate
#define PATH_AMD_PSTATE CPU_ROOT "/amd_pstate/status"

namespace fs = std::filesystem;


class CPUPolicy
{
protected:
  fs::path    mPath;
  std::string mScalingGovernor;
  std::string mScalingSetSpeed;
  int         mScalingMaxFreq;
  int         mScalingMinFreq;

public:
  CPUPolicy(fs::path path) : mPath(path)
  {
    // Get some data.
    mScalingGovernor = sysfs_read(mPath / "scaling_governor");
    mScalingSetSpeed = sysfs_read(mPath / "scaling_setspeed");
    mScalingMaxFreq  = std::stoi(sysfs_read(mPath / "scaling_max_freq"));
    mScalingMinFreq  = std::stoi(sysfs_read(mPath / "scaling_min_freq"));
  }

  virtual ~CPUPolicy()
  {
    try {
      if (mScalingSetSpeed != "<unsupported>")
        sysfs_write(mPath / "scaling_setspeed", mScalingSetSpeed);
      std::cerr << "mScalingGovernor: " << mScalingGovernor << std::endl; //XXX
      sysfs_write(mPath / "scaling_governor", mScalingGovernor);
    }
    catch (std::exception& e) {
      std::cerr << e.what() << '\n';
    }
  }

  virtual void set_frequency(double freq)
  {
    // First, we must set the governor to 'userspace'.
    sysfs_write(mPath / "scaling_governor", "userspace");

    int _freq = mScalingMinFreq;
    if (freq > 1.0) {
      _freq = (int)freq;
    }
    else if ((0.0 <= freq) && (freq <= 1.0)) {
      _freq = (int)mScalingMaxFreq * freq;
    }
    else if (-1.0 == freq) {
      _freq = (int)mScalingMaxFreq;
    }
    else if (-2.0 == freq) {
      _freq = (int)mScalingMinFreq;
    }

    sysfs_write(mPath / "scaling_setspeed",
                _freq < mScalingMinFreq ? mScalingMinFreq : _freq);
  }
};

class CPUPerformanceDriver
{
protected:
  std::vector<CPUPolicy*> mPolicies;

public:
  CPUPerformanceDriver() {}
  virtual ~CPUPerformanceDriver()
  {
    for (CPUPolicy* policy : mPolicies)
      delete policy;
  }

  virtual CPUPolicy* createPolicy(fs::path path)
  {
    return new CPUPolicy(path);
  }

  void setup_policies(CPUSet const& set)
  {
    // Get the vector of CPUPolicy objects that cover the desired CPUs.
    for (auto const& dir_entry : fs::directory_iterator(CPUFREQ_ROOT)) {
      if (!fs::is_directory(dir_entry.path()))
        continue;
      if (std::string::npos ==
          dir_entry.path().filename().string().find("policy"))
        continue;

      // It would be nice if we could use CPUSet's operator>> to read the CPU
      // set. Unfortunately, this is Linux, so the CPU frequence stuff uses
      // a different format.
      std::ifstream in(dir_entry.path() / "affected_cpus");
      while (in.good()) {
        int cpu;
        in >> cpu;
        if (in.fail() || (cpu < 0) || (cpu >= set.max_cpus())) {
          break;
        }
        else if (set.is_set(cpu)) {
          CPUPolicy* policy = this->createPolicy(dir_entry.path());
          mPolicies.push_back(policy);
          break;
        }
      }
    }
  }

  void set_frequency(CPUSet const& set, double freq)
  {
    setup_policies(set);
    for (CPUPolicy* policy : mPolicies)
      policy->set_frequency(freq);
  }

  static CPUPerformanceDriver* create();
};

//
// AMD pstate performance scaling driver
//

class CPUAMDPStatePolicy : public CPUPolicy
{
protected:
  int mLowestNonlinearFreq;

public:
  CPUAMDPStatePolicy(fs::path path) : CPUPolicy(path)
  {
    mLowestNonlinearFreq =
        std::stoi(sysfs_read(mPath / "amd_pstate_lowest_nonlinear_freq"));
  }

  void set_frequency(double freq) override
  {
    if (-3.0 == freq)
      freq = (double)mLowestNonlinearFreq;

    CPUPolicy::set_frequency(freq);
  }
};

class CPUAMDPStatePerformanceDriver : public CPUPerformanceDriver
{
protected:
  std::string mStatus;

public:
  CPUAMDPStatePerformanceDriver() : CPUPerformanceDriver()
  {
    // Get the original status of the amd_pstate governor, then set it to
    // passive.
    mStatus = sysfs_change(PATH_AMD_PSTATE, "passive");
  }

  ~CPUAMDPStatePerformanceDriver() override
  {
    // We need to clear the policies vector before restoring the original state
    // of PATH_AMD_PSTATE, as the policy information is saved after we've set
    // PATH_AMD_PSTATE to 'passive'.
    mPolicies.clear();

    // Restore original state of PATH_AMD_PSTATE.
    sysfs_write(PATH_AMD_PSTATE, mStatus);
  }

  CPUAMDPStatePolicy* createPolicy(fs::path path) override
  {
    return new CPUAMDPStatePolicy(path);
  }
};

CPUPerformanceDriver* CPUPerformanceDriver::create()
{
  if (fs::exists(PATH_AMD_PSTATE))
    return new CPUAMDPStatePerformanceDriver();
  else
    return nullptr;
}

//
// Public interface
//

CPUGovernor::CPUGovernor() : mImpl(nullptr) {}

CPUGovernor::~CPUGovernor()
{
  delete mImpl;
}

void CPUGovernor::set_frequency(CPUSet const& set, double freq)
{
  delete mImpl;
  mImpl = nullptr;

  try {
    if (!(mImpl = CPUPerformanceDriver::create()))
      throw std::bad_alloc();

    mImpl->set_frequency(set, freq);
  }
  catch (const std::exception& e) {
    std::cerr << "Failed to set CPU frequency: " << e.what() << '\n';
    delete mImpl;
    mImpl = nullptr;
  }
}