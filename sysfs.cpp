// sysfs.hpp
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
#include "sysfs.hpp"

#include <iostream>

std::string sysfs_read(std::filesystem::path path)
{
  std::string   result;
  std::ifstream file(path);
  file >> result;
  if (file.fail() && !file.eof())
    throw std::system_error(errno, std::system_category(),
                            std::string("Could not read from \"" +
                                        path.string() + "\""));
  return result;
}

std::string sysfs_change(std::filesystem::path path, std::string value)
{
  std::string  old;
  std::fstream file(path);
  file >> old;
  if (file.fail() && !file.eof())
    throw std::system_error(errno, std::system_category(),
                            std::string("Could not read from \"" +
                                        path.string() + "\""));

  // Output should only follow input after calling one of the file positioning
  // functions or flushing the stream.
  file.seekp(0);
  file << value;
  file.close();
  if (file.fail())
    throw std::system_error(errno, std::system_category(),
                            std::string("Could not write to \"" +
                                        path.string() + "\""));

  return old;
}
