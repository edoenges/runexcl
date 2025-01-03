# CMakeLists.txt for runexcl
cmake_minimum_required(VERSION 3.18)

project(runexcl
  VERSION     1.0.0
  DESCRIPTION "run processes exclusively on cpusets."
  LANGUAGES   CXX
)

# Set the build type to Release by default
if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

# All targets (libraries and executables) should be located in the top-level
# build directory.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Put functions in their own sections to allow the linker to only link the
# things actually used.
string(APPEND CMAKE_C_FLAGS   " -ffunction-sections -fdata-sections")
string(APPEND CMAKE_CXX_FLAGS " -ffunction-sections -fdata-sections")

# runexcl is written in C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# runexcl is for Linux only
add_compile_definitions(_GNU_SOURCE)

# Enable extra warnings for Clang and Gcc
if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wall -Wextra)
endif()

# Put the helper functions into a static library to make unit testing easier.
# The generator expression in target_include_directories is so that we can let
# CMake export the library if we ever want to do that.
add_library(runexcl_utils STATIC
  CPUSet.cpp
  CPUSet.hpp
  CPUCGroup.cpp
  CPUCGroup.hpp
  CPUGovernor.cpp
  CPUGovernor.hpp
  sysfs.cpp
  sysfs.hpp
)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" _include)
target_include_directories(runexcl_utils PUBLIC
  $<BUILD_INTERFACE:${_include}>
  $<INSTALL_INTERFACE:include>
)

add_executable(runexcl runexcl.cpp)
target_link_libraries(runexcl runexcl_utils)

# runexcl must have the SUID bit set and belong to root to be usefull. For now,
# make it so that the PASSWORD environment variable must be set with the
# password used for sudo. Note that if we set the SUID bit and then do the
# chown, the SUID is lost.
add_custom_command(TARGET runexcl POST_BUILD
    COMMAND echo $$PASSWORD | sudo -S chown root:root $<TARGET_FILE:runexcl>
    COMMAND echo $$PASSWORD | sudo -S chmod u+s $<TARGET_FILE:runexcl>
)

#
# Unit tests. While we cannot test runexcl via unit tests, we can test some of
# the support functions.
#
option(WITHOUT_GOOGLETEST "Build without unit tests" ON)
if(NOT WITHOUT_GOOGLETEST)
  find_package(GTest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
