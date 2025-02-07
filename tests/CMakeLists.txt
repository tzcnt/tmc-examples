cmake_minimum_required(VERSION 3.14)
project(tmc_tests)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS "1")
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

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

# copy compile_commands.json to the build directory after build so clangd can find it
# clangd won't look in the /build/{config} directory, only in the top-level /build directory
function(make_exe target_name)
  add_executable(${target_name} ${ARGN})
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/build
  )
endfunction()

make_exe(
  tests
  main.cpp
  test_ex_cpu.cpp
  assert_ex_cpu.cpp
  test_ex_asio.cpp
  test_ex_braid.cpp
  test_spawn_many.cpp
  prio.cpp
)

target_link_libraries(
  tests
  GTest::gtest_main
)

if(TMC_USE_HWLOC AND LIBHWLOC_FOUND)
  target_link_libraries(tests ${LIBHWLOC_LIBRARY})
endif()

if(USE_IO_URING AND LIBURING_FOUND)
  target_compile_definitions(tests PRIVATE ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
  target_link_libraries(tests ${LIBURING_LIBRARY})
endif()

include(GoogleTest)

gtest_discover_tests(tests)
