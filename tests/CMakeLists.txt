cmake_minimum_required(VERSION 3.14)
project(tmc_tests)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS "1")
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(gtest_hide_internal_symbols ON CACHE BOOL "" FORCE)
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

include(GoogleTest)

# GoogleTest library itself (gtest-all.cc) emits a fair amount of warnings noise
target_compile_options(gtest PRIVATE "-w")

# this includes GoogleTest as SYSTEM, but some of its macros are expanded in user code,
# which triggers warnings anyway. disable these specific warnings that GoogleTest causes here.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GCC gives weird object linkage errors with the local lambdas used in some tests
  # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=114267
  add_compile_options("-Wno-subobject-linkage")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  add_compile_options(
    "-Wno-switch-default"
    "-Wno-weak-vtables"
  )
endif()

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
  
  if(WIN32 AND TMC_USE_HWLOC)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${hwloc_SOURCE_DIR}/bin/libhwloc-15.dll
        ${CMAKE_CURRENT_BINARY_DIR}
    )
  endif()
endfunction()

make_exe(
  tests
  main.cpp
  standalone_compilation.cpp
  fixtures/topo_13600k.cpp
  assert_ex_cpu.cpp
  assert_coro.cpp
  assert_spawn.cpp
  assert_spawn_func.cpp
  assert_spawn_many.cpp
  assert_spawn_tuple.cpp
  test_barrier.cpp
  test_latch.cpp
  test_auto_reset_event.cpp
  test_manual_reset_event.cpp
  test_mutex.cpp
  test_semaphore.cpp
  test_ex_any.cpp
  test_ex_cpu.cpp
  test_ex_cpu_st.cpp
  test_ex_manual_st.cpp
  test_ex_asio.cpp
  test_ex_braid.cpp
  test_bitmap_object_pool.cpp
  test_bit_manip.cpp
  test_prio.cpp
  test_atomic_condvar.cpp
  test_channel.cpp
  test_yield.cpp
  test_misc.cpp
  test_coro_functor.cpp
  test_qu_lockfree.cpp
  test_qu_mpsc.cpp
  test_thread_layout.cpp
  test_hwloc.cpp
  test_hwloc_synthetic.cpp
  test_fork_group.cpp
  test_spawn_group.cpp
  test_fork_clang.cpp
)
target_compile_definitions(tests PRIVATE TMC_NO_UNKNOWN_AWAITABLES)

make_exe(test_exceptions test_exceptions.cpp)

make_exe(test_halo test_halo.cpp)
target_compile_definitions(test_halo PRIVATE TMC_DEBUG_TASK_ALLOC_COUNT TMC_NO_UNKNOWN_AWAITABLES)

make_exe(
  test_fuzz_chan
  test_fuzz_chan.cpp
)
target_compile_definitions(test_fuzz_chan PRIVATE TMC_NO_UNKNOWN_AWAITABLES)

if(TMC_USE_HWLOC AND LIBHWLOC_FOUND)
  target_link_libraries(tests ${LIBHWLOC_LIBRARY})
  target_link_libraries(test_exceptions ${LIBHWLOC_LIBRARY})
  target_link_libraries(test_halo ${LIBHWLOC_LIBRARY})
  target_link_libraries(test_fuzz_chan ${LIBHWLOC_LIBRARY})
endif()

gtest_discover_tests(tests)
