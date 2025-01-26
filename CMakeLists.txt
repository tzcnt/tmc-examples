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
    "-march=native"
    "-DTMC_WORK_ITEM=CORO" # one of: CORO, FUNC, FUNCORO, FUNCORO32
    "-DTMC_CUSTOM_CORO_ALLOC"

    # "-DTMC_TRIVIAL_TASK" # enabled in this repo for Release builds via CMakePresets.json
    # "-DTMC_PRIORITY_COUNT=1" # unsigned integer between 1 and 63

    # Other
    # "-DTMC_USE_BOOST_ASIO"

    # Debug / testing options
    # "-DTMC_USE_MUTEXQ"
    # "-DTMC_QUEUE_NO_LIFO"

    # To clean warnings
    # "-Weverything"
    # "-Wno-c++98-compat"
    # "-Wno-c++98-compat-pedantic"
    # "-Wno-c++20-compat"
    # "-Wno-padded"
    # "-Wno-unsafe-buffer-usage"
    # "-Wno-global-constructors"
    # "-Wno-exit-time-destructors"
    # "-Wno-sign-conversion"
    # "-Wno-deprecated-declarations"
)

include(cmake/CPM.cmake)

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
        GIT_TAG ff67f117451d11b54d7d193135244981ef06c9be
        DOWNLOAD_ONLY)

    CPMAddPackage(
        NAME tmc_asio
        GIT_REPOSITORY https://github.com/tzcnt/tmc-asio.git
        GIT_TAG fa810b94f406ca15164ed4932eaca1204890171a
        DOWNLOAD_ONLY)

    include_directories(
        ${TooManyCooks_SOURCE_DIR}/include
        ${tmc_asio_SOURCE_DIR}/include
    )
endif()

CPMAddPackage(
    NAME asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    VERSION 1.28.1
    GIT_TAG asio-1-28-1
    DOWNLOAD_ONLY)

# mark Asio headers as SYSTEM to ignore warnings
include_directories(SYSTEM
    ${asio_SOURCE_DIR}/asio/include
)

# TMC HWLOC support
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

# URING support - this isn't a TMC flag, but an Asio option (for performance)
option(USE_IO_URING "io_uring (requires Linux and liburing)" ON)

if(USE_IO_URING)
    if(NOT CMAKE_SYSTEM_NAME MATCHES Linux)
        set(USE_IO_URING OFF CACHE BOOL "feature disabled (requires Linux)" FORCE)
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

option(TMC_CHECK_WARNINGS_CLANG "exhaustive warning checks with clang's -Weverything" OFF)

if(TMC_CHECK_WARNINGS_CLANG)
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

        # Ignore these (from some quick and dirty examples)
        "-Wno-sign-conversion"
        "-Wno-missing-prototypes"
        "-Wno-missing-variable-declarations"
    )
endif()

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

add_executable(external_coro
    examples/external_coro.cpp
)

add_executable(external_executor
    examples/external_executor.cpp
)

add_executable(external_awaitable
    examples/external_awaitable.cpp
)

add_executable(braid
    examples/braid.cpp
)

add_executable(skynet
    examples/skynet/main.cpp
)
target_include_directories(skynet PRIVATE examples/skynet)

add_executable(fib
    examples/fib.cpp
)

add_executable(sync
    examples/sync.cpp
)

add_executable(spawn_iterator
    examples/spawn_iterator.cpp
)

add_executable(asio_prio
    examples/asio/prio.cpp
)
target_link_libraries(asio_prio ${LIBURING_LIBRARY})

add_executable(asio_cancellation
    examples/asio/cancellation.cpp
)
target_link_libraries(asio_cancellation ${LIBURING_LIBRARY})

add_executable(asio_delay
    examples/asio/delay.cpp
)
target_link_libraries(asio_delay ${LIBURING_LIBRARY})

add_executable(asio_timer_mem_bench
    examples/asio/timer_mem_bench.cpp
)
target_link_libraries(asio_timer_mem_bench ${LIBURING_LIBRARY})

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

add_custom_command(TARGET asio_cancellation POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET asio_delay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET asio_timer_mem_bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET asio_prio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)

add_custom_command(TARGET spawn_iterator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)
