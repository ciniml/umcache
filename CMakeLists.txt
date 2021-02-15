cmake_minimum_required(VERSION 3.8)
project(userfaultfd)

add_subdirectory(test/googletest)

find_package(OpenCV REQUIRED)

add_executable(test_main
  test/test_main.cpp
  test/test_basic.cpp
  src/umcache.cpp)

target_link_libraries(test_main
  gtest_main
  pthread)
target_include_directories(test_main PUBLIC
  src
  test/googletest/googletest/include)