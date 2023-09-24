cmake_minimum_required(VERSION 3.16)
project(tmc_examples)

set(CMAKE_MODULE_PATH
    ${tmc_examples_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH})

set(CMAKE_EXPORT_COMPILE_COMMANDS "1")

# set(CMAKE_CXX20_STANDARD_COMPILE_OPTION "-std=c++20")
set(CMAKE_CXX_STANDARD 20)

add_definitions(
    "-march=native"

    # "-DTMC_WORK_ITEM=CORO" # one of: CORO, FUNC, FUNCORO, FUNCORO32

    # "-DTMC_PRIORITY_COUNT=2" # unsigned integer >= 1
    # "-DTMC_USE_BOOST_ASIO"

    # Debug / testing options
    # "-DTMC_USE_MUTEXQ"
    # "-DTMC_NO_LIFO"
)

# HWLOC support
option(TMC_USE_HWLOC "libhwloc" ON)

if(TMC_USE_HWLOC)
    message(STATUS "TMC_USE_HWLOC: running find_package(libhwloc)...")
    find_package(libhwloc)

    if(NOT LIBHWLOC_FOUND)
        message(FATAL_ERROR "TMC_USE_HWLOC: ERROR package libhwloc not found")
    else()
        message(STATUS "TMC_USE_HWLOC: using ${LIBHWLOC_LIBRARY}")
        add_compile_definitions(TMC_USE_HWLOC)
        link_libraries(${LIBHWLOC_LIBRARY})
    endif()
endif()

# URING support
option(TMC_USE_IO_URING "io_uring (requires Linux and liburing)" ON)

if(TMC_USE_IO_URING)
    if(NOT CMAKE_SYSTEM_NAME MATCHES Linux)
        set(TMC_USE_IO_URING OFF CACHE BOOL "feature disabled (requires Linux)" FORCE)
        message(STATUS "TMC_USE_IO_URING: WARN feature disabled (requires Linux)")
    else()
        message(STATUS "TMC_USE_IO_URING: running find_package(liburing)...")
        find_package(liburing)

        if(NOT LIBURING_FOUND)
            message(FATAL_ERROR "TMC_USE_IO_URING: ERROR package liburing not found")
        else()
            message(STATUS "TMC_USE_IO_URING: using ${LIBURING_LIBRARY}")
            add_compile_definitions(ASIO_HAS_IO_URING ASIO_DISABLE_EPOLL)
        endif()
    endif()
endif()

include(cmake/CPM.cmake)
CPMAddPackage(
    NAME asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    VERSION 1.28.1
    GIT_TAG asio-1-28-1
    DOWNLOAD_ONLY)

# By default, download the TMC repos as CPM packages.
# If this option is enabled, CMake will expect them to be in git submodules.
# You must run `submodule update --init --recursive` before turning this on.
# This option exists to simplify my development across repositories.
option(TMC_AS_SUBMODULE "Download TMC repos as Git submodules" OFF)

if(TMC_AS_SUBMODULE)
    include_directories(
        ${tmc_examples_SOURCE_DIR}/submodules/TooManyCooks/include
        ${tmc_examples_SOURCE_DIR}/submodules/tmc-asio/include
    )
else()
    CPMAddPackage(
        NAME TooManyCooks
        GIT_REPOSITORY https://github.com/tzcnt/TooManyCooks.git
        GIT_TAG 5beb14cfcb86e0a92cc54657328ef75a7100a97c
        DOWNLOAD_ONLY)

    CPMAddPackage(
        NAME tmc_asio
        GIT_REPOSITORY https://github.com/tzcnt/tmc-asio.git
        GIT_TAG b1496fc
        DOWNLOAD_ONLY)

    include_directories(
        ${TooManyCooks_SOURCE_DIR}/include
        ${tmc_asio_SOURCE_DIR}/include
    )
endif()

include_directories(
    ${asio_SOURCE_DIR}/asio/include
)

# Since each new coroutine requires an allocation,
# they are sensitive to allocator performance.
# All of jemalloc, mimalloc, and tcmalloc provide
# greatly superior performance to the default glibc malloc.
# Try to find any of these 3 before falling back to default.
find_package(libjemalloc)

if(LIBJEMALLOC_FOUND)
    set(MALLOC_LIB "${LIBJEMALLOC_LIBRARY}")
    message(STATUS "Using malloc: ${MALLOC_LIB}")
else()
    find_package(libmimalloc)

    if(LIBMIMALLOC_FOUND)
        set(MALLOC_LIB "${LIBMIMALLOC_LIBRARY}")
        message(STATUS "Using malloc: ${MALLOC_LIB}")
    else()
        find_package(libtcmalloc)

        if(LIBTCMALLOC_FOUND)
            set(MALLOC_LIB "${LIBTCMALLOC_LIBRARY}")
            message(STATUS "Using malloc: ${MALLOC_LIB}")
        else()
            message(STATUS "Using malloc: default")
        endif()
    endif()
endif()

link_libraries(${MALLOC_LIB})

add_executable(alignment
    examples/alignment.cpp
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    target_compile_options(alignment PRIVATE -fcoro-aligned-allocation)
endif()

add_executable(spawn_misc
    examples/spawn_misc.cpp
)

add_executable(equilibrium
    examples/equilibrium.cpp
)

add_executable(braid
    examples/braid.cpp
)

# braid requires aligned allocation since it creates braids (which have queues, which use alignas)
# inside of coroutines
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    target_compile_options(braid PRIVATE -fcoro-aligned-allocation)
endif()

add_executable(skynet
    examples/skynet/main.cpp
)
target_include_directories(skynet PRIVATE examples/skynet)

add_executable(skynet_prio
    examples/skynet/priorities/main.cpp
)
target_include_directories(skynet_prio PRIVATE examples/skynet)

add_executable(fib
    examples/fib.cpp
)

add_executable(delay
    examples/asio/delay.cpp
)
target_link_libraries(delay ${LIBURING_LIBRARY})

add_executable(asio_http_server
    examples/asio/http_server.cpp
)
target_link_libraries(asio_http_server ${LIBURING_LIBRARY})

add_executable(asio_http_skynet
    examples/asio/http_skynet.cpp
)
target_link_libraries(asio_http_skynet ${LIBURING_LIBRARY})

# copy compile_commands.json to the root directory after build so clangd can find it
# clangd won't look in the /build/{config} directory, only in the top-level /build directory
add_custom_command(TARGET spawn_misc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET alignment POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET equilibrium POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET skynet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET skynet_prio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET fib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET braid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET delay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)
