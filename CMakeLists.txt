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
    set(TMC_INCLUDE_PATH ${tmc_examples_SOURCE_DIR}/submodules/TooManyCooks/include CACHE STRING "" FORCE)
    set(TMC_ASIO_INCLUDE_PATH ${tmc_examples_SOURCE_DIR}/submodules/tmc-asio/include CACHE STRING "" FORCE)
else()
    CPMAddPackage(
        NAME TooManyCooks
        GIT_REPOSITORY https://github.com/tzcnt/TooManyCooks.git
        GIT_TAG dc244504369f60f85689f089419b2c40f0205b64
        DOWNLOAD_ONLY)

    CPMAddPackage(
        NAME tmc_asio
        GIT_REPOSITORY https://github.com/tzcnt/tmc-asio.git
        GIT_TAG 18f5813bd2efafbad52f8ae8aa2a8803f9ce8619
        DOWNLOAD_ONLY)

    set(TMC_INCLUDE_PATH ${TooManyCooks_SOURCE_DIR}/include CACHE STRING "" FORCE)
    set(TMC_ASIO_INCLUDE_PATH ${tmc_asio_SOURCE_DIR}/include CACHE STRING "" FORCE)
endif()

include_directories(
    ${TMC_INCLUDE_PATH}
    ${TMC_ASIO_INCLUDE_PATH}
)

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
        message(STATUS "TMC_USE_HWLOC: WARN package libhwloc not found")
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
        message(STATUS "USE_IO_URING: WARN feature disabled (requires Linux)")
    else()
        message(STATUS "USE_IO_URING: running find_package(liburing)...")
        find_package(liburing)

        if(NOT LIBURING_FOUND)
            message(STATUS "USE_IO_URING: WARN package liburing not found")
        else()
            message(STATUS "USE_IO_URING: using ${LIBURING_LIBRARY}")
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

if(USE_IO_URING AND LIBURING_FOUND)
    target_link_libraries(asio_prio ${LIBURING_LIBRARY})
    target_link_libraries(asio_cancellation ${LIBURING_LIBRARY})
    target_link_libraries(asio_delay ${LIBURING_LIBRARY})
    target_link_libraries(asio_timer_mem_bench ${LIBURING_LIBRARY})
    target_link_libraries(asio_http_server ${LIBURING_LIBRARY})
    target_link_libraries(asio_http_skynet ${LIBURING_LIBRARY})
endif()

add_subdirectory(tests)
