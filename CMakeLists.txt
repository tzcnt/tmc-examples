cmake_minimum_required(VERSION 3.16)
project(tmc_examples)

set(CMAKE_MODULE_PATH
    ${tmc_examples_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH})

set(CMAKE_EXPORT_COMPILE_COMMANDS "1")

set(CMAKE_CXX_STANDARD 20)

# Optional flags can be passed at the command line. Used by GitHub Actions workflow.
if(CMD_COMPILE_FLAGS)
    message(STATUS "CMD_COMPILE_FLAGS: ${CMD_COMPILE_FLAGS}")
    add_compile_options(${CMD_COMPILE_FLAGS})
endif()

if(CMD_LINK_FLAGS)
    message(STATUS "CMD_LINK_FLAGS: ${CMD_LINK_FLAGS}")
    add_link_options(${CMD_LINK_FLAGS})
endif()

include(cmake/CPM.cmake)

# TooManyCooks options. Set these before add_subdirectory()/CPMAddPackage()
# so they are applied to the TooManyCooks::TooManyCooks interface target.
# Defaults are specified below.
option(TMC_USE_HWLOC "Enable hwloc topology-aware thread placement" ON)
option(TMC_USE_BOOST_ASIO "Use boost::asio instead of standalone asio" OFF)

set(TMC_WORK_ITEM "CORO" CACHE STRING "Work item type: CORO or FUNCORO")
set_property(CACHE TMC_WORK_ITEM PROPERTY STRINGS "CORO" "FUNCORO")

option(TMC_TRIVIAL_TASK "Enable trivial task optimization" OFF)
option(TMC_NODISCARD_AWAIT "Enable [[nodiscard]] on co_await" OFF)

set(TMC_PRIORITY_COUNT "" CACHE STRING "Number of priority levels (1-16, empty for runtime-configurable)")

option(TMC_MORE_THREADS "Allow more than 64 threads" OFF)
option(TMC_DEBUG_TASK_ALLOC_COUNT "Track task allocation count for HALO analysis" OFF)
option(TMC_DEBUG_THREAD_CREATION "Print thread creation and work stealing info" OFF)

option(TMC_STANDALONE_COMPILATION "Disable header-only mode. Library will be built into the file that defines TMC_IMPL" OFF)

if(WIN32)
    option(TMC_WINDOWS_DLL "Enable Windows DLL mode" OFF)
endif()

# On Windows, download hwloc binaries before adding TooManyCooks
# so that its CMakeLists.txt can find them.
if(WIN32 AND TMC_USE_HWLOC)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PLATFORM_BITS 64)
    else()
        set(PLATFORM_BITS 32)
    endif()

    CPMAddPackage(
        NAME hwloc
        URL "https://download.open-mpi.org/release/hwloc/v2.12/hwloc-win${PLATFORM_BITS}-build-2.12.2.zip"
        VERSION 2.12.2
        DOWNLOAD_ONLY
    )

    add_library(hwloc::hwloc UNKNOWN IMPORTED)
    set_target_properties(hwloc::hwloc PROPERTIES
        IMPORTED_LOCATION "${hwloc_SOURCE_DIR}/lib/libhwloc.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${hwloc_SOURCE_DIR}/include"
    )
endif()

# By default, download the TMC repos as CPM packages.
# If this option is enabled, CMake will expect them to be in git submodules.
# You must run `submodule update --init --recursive` before turning this on.
# This option exists to simplify my development across repositories.
option(TMC_AS_SUBMODULE "Download TMC repos as Git submodules" OFF)

if(TMC_AS_SUBMODULE)
    add_subdirectory(submodules/TooManyCooks)
else()
    CPMAddPackage(
        NAME TooManyCooks
        GIT_REPOSITORY https://github.com/tzcnt/TooManyCooks.git
        GIT_TAG main)
endif()

# Since each new coroutine requires an allocation,
# they are sensitive to allocator performance.
# Any of tcmalloc, mimalloc, or jemalloc provide
# greatly superior performance to the default glibc malloc.
# Try to find any of these 3 before falling back to default.
find_package(libtcmalloc)

if(LIBTCMALLOC_FOUND)
    set(MALLOC_LIB "${LIBTCMALLOC_LIBRARY}")
    message(STATUS "Using malloc: ${MALLOC_LIB}")
else()
    find_package(libmimalloc)

    if(LIBMIMALLOC_FOUND)
        set(MALLOC_LIB "${LIBMIMALLOC_LIBRARY}")
        message(STATUS "Using malloc: ${MALLOC_LIB}")
    else()
        find_package(libjemalloc)

        if(LIBJEMALLOC_FOUND)
            set(MALLOC_LIB "${LIBJEMALLOC_LIBRARY}")
            message(STATUS "Using malloc: ${MALLOC_LIB}")
        else()
            message(STATUS "WARNING: No high performance allocator was found. Using default system allocator.")
        endif()
    endif()
endif()

link_libraries(${MALLOC_LIB})

# Standalone asio or boost::asio support
# The TooManyCooks::TooManyCooks target propagates the TMC_USE_BOOST_ASIO define, but does not
# provide the asio or boost::asio headers. The consumer must make them available
# (e.g. via find_package, CPM, or system install) before including tmc-asio headers.
if(TMC_USE_BOOST_ASIO)
    # Tell cmake to use the target Boost cmake file instead of the built-in
    cmake_policy(SET CMP0167 NEW)
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_RELEASE_LIBS ON)

    # By default this will will look for your system boost.
    # If this fails you can install boost manually and set the system environment variable
    # BOOST_CMAKE_ROOT with the /stage/lib/cmake directory being where boost places the CMake files after building with b2.
    if(DEFINED ENV{BOOST_CMAKE_ROOT})
        find_package(
            Boost 1.82.0 REQUIRED CONFIG
            PATHS $ENV{BOOST_CMAKE_ROOT} NO_DEFAULT_PATH
        )
    else()
        find_package(Boost 1.82.0 REQUIRED)
    endif()
else()
    CPMAddPackage(
        NAME asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        VERSION 1.28.1
        GIT_TAG asio-1-28-1
        DOWNLOAD_ONLY)
    set(ASIO_INCLUDE_PATH ${asio_SOURCE_DIR}/asio/include CACHE STRING "" FORCE)

    # mark Asio headers as SYSTEM to ignore warnings
    include_directories(SYSTEM
        ${ASIO_INCLUDE_PATH}
    )
endif()

if(WIN32)
    # Prevent Windows from automatically including "winsock.h" which conflicts with Asio
    add_compile_definitions("WIN32_LEAN_AND_MEAN")
endif()

# Build tests and examples with most pedantic warnings possible
# to ensure the library remains clean for users
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_definitions(
        "-Weverything"

        # This library requires C++20
        "-Wno-c++98-compat"
        "-Wno-c++98-compat-pedantic"
        "-Wno-c++20-compat"

        # These are deliberate design choices of the library
        "-Wno-padded"
        "-Wno-unsafe-buffer-usage"
        "-Wno-global-constructors"
        "-Wno-exit-time-destructors"
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
        add_definitions(

            # Obnoxious spurious warning, wtf Apple
            "-Wno-poison-system-directories"
        )
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_definitions(
        "-Wall"
        "-Wextra"
        "-Wpedantic"
    )
endif()

# Sanitizers

# add_definitions("-fsanitize=address")
# add_link_options("-fsanitize=address")
# add_definitions("-fsanitize=thread")
# add_link_options("-fsanitize=thread")
# add_definitions("-fsanitize=undefined -fno-sanitize-recover")
# add_link_options("-fsanitize=undefined")

# Demonstrating how to link TMC into a DLL
if(WIN32)
    option(TMC_USE_DLL "Link all targets against tmc_dll shared library. Requires TMC_WINDOWS_DLL=ON" OFF)

    if(TMC_USE_DLL AND NOT TMC_WINDOWS_DLL)
        message(FATAL_ERROR "TMC_USE_DLL requires TMC_WINDOWS_DLL=ON")
    endif()

    # A shared library (DLL) containing the TMC standalone compilation
    add_library(tmc_dll SHARED
        examples/standalone_compilation.cpp
    )
    target_link_libraries(tmc_dll PRIVATE TooManyCooks::TooManyCooks)
    target_compile_definitions(tmc_dll PRIVATE TMC_WINDOWS_DLL)

    if(TMC_USE_HWLOC)
        add_custom_command(TARGET tmc_dll POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${hwloc_SOURCE_DIR}/bin/libhwloc-15.dll
            ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif()
endif()

if(TMC_STANDALONE_COMPILATION AND NOT TMC_USE_DLL)
    add_library(tmc_standalone_compilation OBJECT
        examples/standalone_compilation.cpp
    )
    target_link_libraries(tmc_standalone_compilation PRIVATE TooManyCooks::TooManyCooks)
endif()

function(make_exe target_name)
    add_executable(${target_name} ${ARGN})

    target_link_libraries(${target_name} PRIVATE TooManyCooks::TooManyCooks)

    if(TMC_STANDALONE_COMPILATION AND NOT TMC_USE_DLL)
        target_link_libraries(${target_name} PRIVATE tmc_standalone_compilation)
    endif()

    # copy compile_commands.json to the build directory after build so clangd can find it
    # clangd won't look in the /build/{PRESET} directory, only in the top-level /build directory

    # this often fails on GitHub Actions for some reason, so disable it there
    option(TMC_COPY_COMPILE_COMMANDS "Copy compile_commands.json to the top-level build directory after each target builds" ON)

    if(TMC_COPY_COMPILE_COMMANDS)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_CURRENT_LIST_DIR}/build
        )
    endif()

    if(TMC_USE_DLL)
        target_link_libraries(${target_name} PRIVATE tmc_dll)
    endif()

    if(WIN32 AND TMC_USE_HWLOC)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${hwloc_SOURCE_DIR}/bin/libhwloc-15.dll
            ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif()
endfunction()

make_exe(alignment
    examples/alignment.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
        target_compile_options(alignment PRIVATE -fcoro-aligned-allocation)
    endif()
endif()

make_exe(external_callback_awaitable
    examples/external/callback_awaitable.cpp
)

make_exe(external_coro
    examples/external/external_coro.cpp
)

make_exe(external_executor
    examples/external/external_executor.cpp
)

make_exe(external_awaitable
    examples/external/external_awaitable.cpp
)

make_exe(exec_st_roundtrip_bench
    examples/exec_st_roundtrip_bench.cpp
)

make_exe(braid
    examples/braid.cpp
)

make_exe(skynet
    examples/skynet.cpp
)

make_exe(fib
    examples/fib.cpp
)

make_exe(chan_bench
    examples/chan_bench.cpp
)

make_exe(queue_bench
    examples/queue_bench.cpp
)

make_exe(sync
    examples/sync.cpp
)

make_exe(spawn_iterator
    examples/spawn_iterator.cpp
)

make_exe(pipeline
    examples/pipeline.cpp
)

make_exe(pipeline_fifo
    examples/pipeline_fifo.cpp
)

make_exe(asio_prio
    examples/asio/prio.cpp
)

make_exe(asio_mux_tuple
    examples/asio/mux_tuple.cpp
)

make_exe(asio_select
    examples/asio/select.cpp
)

make_exe(asio_delay
    examples/asio/delay.cpp
)

make_exe(asio_batch_processor
    examples/asio/batch_processor.cpp
)

make_exe(asio_timer_mem_bench
    examples/asio/timer_mem_bench.cpp
)

make_exe(asio_http_server
    examples/asio/http_server.cpp
)

make_exe(asio_http_skynet
    examples/asio/http_skynet.cpp
)

make_exe(hwloc_asio_thread_per_core
    examples/hwloc/asio_thread_per_core.cpp
)

make_exe(hwloc_asio_server_per_cache
    examples/hwloc/asio_server_per_cache.cpp
)

make_exe(hwloc_hybrid_executor
    examples/hwloc/hybrid_executor.cpp
)

make_exe(hwloc_topo
    examples/hwloc/topo.cpp
)

add_subdirectory(tests)
