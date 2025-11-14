cmake_minimum_required(VERSION 3.16)
project(tmc_examples)

set(CMAKE_MODULE_PATH
    ${tmc_examples_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH})

set(CMAKE_EXPORT_COMPILE_COMMANDS "1")

# set(CMAKE_CXX20_STANDARD_COMPILE_OPTION "-std=c++20")
set(CMAKE_CXX_STANDARD 20)

add_definitions(

    # Performance tuning options
    # "-DTMC_WORK_ITEM=CORO" # one of: CORO, FUNC, FUNCORO

    # "-DTMC_TRIVIAL_TASK" # enabled in this repo for Release builds via CMakePresets.json
    # "-DTMC_PRIORITY_COUNT=1" # unsigned integer between 1 and 63

    # Debug / diagnostics options
    # tracks the number of tmc::task allocations which lets you determine
    # how effectively Heap Allocation Elision (HALO) is being applied by the compiler
    # "-DTMC_DEBUG_TASK_ALLOC_COUNT"
)

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

# By default, download the TMC repos as CPM packages.
# If this option is enabled, CMake will expect them to be in git submodules.
# You must run `submodule update --init --recursive` before turning this on.
# This option exists to simplify my development across repositories.
option(TMC_AS_SUBMODULE "Download TMC repos as Git submodules" OFF)

if(TMC_AS_SUBMODULE)
    set(TMC_INCLUDE_PATH ${tmc_examples_SOURCE_DIR}/submodules/TooManyCooks/include CACHE STRING "" FORCE)
    set(TMC_ASIO_INCLUDE_PATH ${tmc_examples_SOURCE_DIR}/submodules/tmc-asio/include CACHE STRING "" FORCE)
else()
    CPMAddPackage(
        NAME TooManyCooks
        GIT_REPOSITORY https://github.com/tzcnt/TooManyCooks.git
        GIT_TAG 78612ffde6b310252f90adf4413ea7c32f0c306b
        DOWNLOAD_ONLY)

    CPMAddPackage(
        NAME tmc_asio
        GIT_REPOSITORY https://github.com/tzcnt/tmc-asio.git
        GIT_TAG 14a2f71b9ce7d1e24a901c1b6d57b09da734e274
        DOWNLOAD_ONLY)

    set(TMC_INCLUDE_PATH ${TooManyCooks_SOURCE_DIR}/include CACHE STRING "" FORCE)
    set(TMC_ASIO_INCLUDE_PATH ${tmc_asio_SOURCE_DIR}/include CACHE STRING "" FORCE)
endif()

include_directories(
    ${TMC_INCLUDE_PATH}
    ${TMC_ASIO_INCLUDE_PATH}
)

# TMC HWLOC support
option(TMC_USE_HWLOC "libhwloc" ON)

if(TMC_USE_HWLOC)
    message(STATUS "TMC_USE_HWLOC: running find_package(libhwloc)...")
    find_package(libhwloc)

    if(NOT LIBHWLOC_FOUND)
        message(STATUS "TMC_USE_HWLOC: WARN package libhwloc not found")
    else()
        message(STATUS "TMC_USE_HWLOC: found library ${LIBHWLOC_LIBRARY}")
        message(STATUS "TMC_USE_HWLOC: found include ${LIBHWLOC_INCLUDE_DIR}/hwloc.h")
        add_compile_definitions(TMC_USE_HWLOC)
        link_libraries(${LIBHWLOC_LIBRARY})
        include_directories(${LIBHWLOC_INCLUDE_DIR})
    endif()
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
            message(STATUS "Using malloc: default")
        endif()
    endif()
endif()

link_libraries(${MALLOC_LIB})

# Standalone asio or boost::asio support
option(TMC_USE_BOOST_ASIO "Use boost::asio instead of standalone asio" OFF)

if(TMC_USE_BOOST_ASIO)
    add_compile_definitions(TMC_USE_BOOST_ASIO)

    # Tell cmake to use the target Boost cmake file instead of the built-in
    cmake_policy(SET CMP0167 NEW)
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_RELEASE_LIBS ON)

    # set(ENV{BOOST_CMAKE_ROOT} "/home/tzcnt/pkg-src/boost-1.88-src/boost_1_88_0/stage/lib/cmake/")

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

# Safety / warnings checks

# add_definitions("-fsanitize=address")
# add_link_options("-fsanitize=address")
# add_definitions("-fsanitize=thread")
# add_link_options("-fsanitize=thread")
# add_definitions("-fsanitize=undefined -fno-sanitize-recover")
# add_link_options("-fsanitize=undefined")

# extra warnings on Clang
add_definitions(
"-Weverything"

# Ignore these (from the library)
"-Wno-c++98-compat"
"-Wno-c++98-compat-pedantic"
"-Wno-c++20-compat"
"-Wno-padded"
"-Wno-unsafe-buffer-usage"
"-Wno-global-constructors"
"-Wno-exit-time-destructors"
"-Wno-unique-object-duplication"

# Ignore these (from some quick and dirty examples)
"-Wno-missing-variable-declarations"
"-Wno-double-promotion"
)

# extra warnings on GCC
# add_definitions(
# "-Wall"
# "-Wextra"

# "-Wno-pessimizing-move"
# )

# copy compile_commands.json to the build directory after build so clangd can find it
# clangd won't look in the /build/{config} directory, only in the top-level /build directory
function(make_exe target_name)
    add_executable(${target_name} ${ARGN})
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_CURRENT_LIST_DIR}/build
    )
endfunction()

make_exe(alignment
    examples/alignment.cpp
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    target_compile_options(alignment PRIVATE -fcoro-aligned-allocation)
endif()

make_exe(spawn_misc
    examples/spawn_misc.cpp
)

make_exe(equilibrium
    examples/equilibrium.cpp
)

make_exe(callback_awaitable
    examples/callback_awaitable.cpp
)

make_exe(external_coro
    examples/external_coro.cpp
)

make_exe(external_executor
    examples/external_executor.cpp
)

make_exe(external_awaitable
    examples/external_awaitable.cpp
)

make_exe(braid
    examples/braid.cpp
)

make_exe(skynet
    examples/skynet/main.cpp
)
target_include_directories(skynet PRIVATE examples/skynet)

make_exe(fib
    examples/fib.cpp
)

make_exe(chan_bench
    examples/chan_bench.cpp
)

make_exe(braid_bench
    examples/braid_bench.cpp
)

make_exe(sync
    examples/sync.cpp
)

make_exe(spawn_iterator
    examples/spawn_iterator.cpp
)

make_exe(asio_prio
    examples/asio/prio.cpp
)

make_exe(asio_cancellation
    examples/asio/cancellation.cpp
)

make_exe(asio_delay
    examples/asio/delay.cpp
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

add_subdirectory(tests)
