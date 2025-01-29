cmake_minimum_required(VERSION 3.14)
project(tmc_tests)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

CPMAddPackage(
  NAME googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  DOWNLOAD_ONLY)

enable_testing()

# Variables provided by parent project
include_directories(
  ${TMC_INCLUDE_PATH}
  ${TMC_ASIO_INCLUDE_PATH}
)

add_executable(
  test1
  tmc_impl.cpp
  test1.cpp
)
target_link_libraries(
  test1
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test1)
