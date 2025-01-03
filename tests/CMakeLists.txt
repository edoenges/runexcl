# CMakeLists.txt for runexcl unit tests

add_executable(runexcl_tests
  CPUSet_tests.cpp
)

target_link_libraries(runexcl_tests runexcl_utils GTest::gtest_main)

# Register with ctest
add_test(NAME runexcl_tests
  COMMAND $<TARGET_FILE:runexcl_tests
    --gtest_output=xml:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/runexcl_tests.xml
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)