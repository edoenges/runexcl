// sysfs.hpp
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
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

//! Read a string from the specified path.
//! \param in path Path to sysfs file to read from
//! \return std::string read from sysfs file with the '<<' operator.
std::string sysfs_read(std::filesystem::path path);

//! Read a string from the specified path, then write the new value to it.
std::string sysfs_change(std::filesystem::path path, std::string value);

template<typename T>
void sysfs_write(std::filesystem::path path, T const& value)
{
  std::ofstream file(path);
  file << value;
  file.close();
  if (file.fail())
    throw std::system_error(errno, std::system_category(),
                            std::string("Could not write to \"" +
                                        path.string() + "\""));
}
