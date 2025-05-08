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

# mark Asio headers as SYSTEM to ignore warnings
include_directories(SYSTEM
  ${ASIO_INCLUDE_PATH}
)

# copy compile_commands.json to the build directory after build so clangd can find it
# clangd won't look in the /build/{config} directory, only in the top-level /build directory
function(make_exe target_name)
  add_executable(${target_name} ${ARGN})
  target_link_libraries(${target_name} GTest::gtest_main)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/build
  )
endfunction()

make_exe(
  tests
  main.cpp
  standalone_compilation.cpp
  assert_ex_cpu.cpp
  assert_coro.cpp
  assert_spawn.cpp
  assert_spawn_func.cpp
  assert_spawn_many.cpp
  assert_spawn_tuple.cpp
  test_barrier.cpp
  test_mutex.cpp
  test_semaphore.cpp
  test_ex_cpu.cpp
  test_ex_asio.cpp
  test_ex_braid.cpp
  test_prio.cpp
  test_atomic_condvar.cpp
  test_channel.cpp
  test_yield.cpp
  test_misc.cpp
  test_qu_lockfree.cpp
  test_thread_layout.cpp
)
target_compile_definitions(tests PRIVATE TMC_NO_UNKNOWN_AWAITABLES)

# GCC gives weird object linkage errors with the local lambdas used in some tests
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=114267
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(tests PRIVATE -Wno-subobject-linkage)
endif()

make_exe(
  test_fuzz_chan
  test_fuzz_chan.cpp
)
target_compile_definitions(test_fuzz_chan PRIVATE TMC_NO_UNKNOWN_AWAITABLES)

make_exe(test_exceptions test_exceptions.cpp)

if(TMC_USE_HWLOC AND LIBHWLOC_FOUND)
  target_link_libraries(tests ${LIBHWLOC_LIBRARY})
  target_link_libraries(test_fuzz_chan ${LIBHWLOC_LIBRARY})
  target_link_libraries(test_exceptions ${LIBHWLOC_LIBRARY})
endif()

include(GoogleTest)

gtest_discover_tests(tests)
