cmake_minimum_required(VERSION 3.20)
project(focusZ-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Google Test
find_package(GTest REQUIRED)

# Add test executable
add_executable(focusZ-tests test_depth_focus.cpp)

# Link against Google Test
target_link_libraries(focusZ-tests
    GTest::GTest
    GTest::Main
)

# Enable CTest
enable_testing()
add_test(NAME focusZ-tests COMMAND focusZ-tests)
