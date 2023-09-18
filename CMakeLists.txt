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

    "-DTMC_WORK_ITEM=CORO" # one of: CORO, FUNC, FUNCORO, FUNCORO32

    # "-DTMC_PRIORITY_COUNT=2"
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

# By default, download the TMC repos as git submodules. This simplifies my development, and aids in code exploration.
# If this option is disabled, they will be downloaded as CPM packages instead.
option(TMC_AS_SUBMODULE "Download TMC repos as Git submodules" ON)

if(TMC_AS_SUBMODULE)
    find_package(Git QUIET)

    if(NOT GIT_FOUND)
        message(FATAL_ERROR "Git not found!")
    endif()

    message(STATUS "Updating git submodules...")
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_SUBMOD_RESULT)

    if(NOT GIT_SUBMOD_RESULT EQUAL "0")
        message(FATAL_ERROR "`git submodule update --init --recursive` failed with ${GIT_SUBMOD_RESULT}. Please checkout submodules manually.")
    endif()

    include_directories(
        ${tmc_examples_SOURCE_DIR}/submodules/TooManyCooks/include
        ${tmc_examples_SOURCE_DIR}/submodules/tmc-asio/include
    )
else()
    CPMAddPackage(
        NAME TooManyCooks
        GIT_REPOSITORY github-as-tzcnt:tzcnt/TooManyCooks.git
        GIT_TAG b6ff034
        DOWNLOAD_ONLY)

    CPMAddPackage(
        NAME tmc_asio
        GIT_REPOSITORY github-as-tzcnt:tzcnt/tmc-asio.git
        GIT_TAG 8d1c6eb
        DOWNLOAD_ONLY)

    include_directories(
        ${TooManyCooks_SOURCE_DIR}/include
        ${tmc_asio_SOURCE_DIR}/include
    )
endif()

include_directories(
    ${asio_SOURCE_DIR}/asio/include
)

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

add_executable(runtime
    examples/runtime.cpp
)

add_executable(asio_http_server
    examples/asio/http_server.cpp
)

add_executable(asio_http_skynet
    examples/asio/http_skynet.cpp
)

if(UNIX)
    find_library(JEMALLOC_LIBRARY NAMES
        jemalloc
        libjemalloc.so.2
        REQUIRED
    )

    # find_library(TCMALLOC_LIBRARY NAMES
    # tcmalloc_minimal
    # libtcmalloc_minimal.so.4*
    # tcmalloc
    # libtcmalloc.so.4
    # REQUIRED
    # )
    target_link_libraries(alignment ${LIBHWLOC_LIBRARY})
    target_link_libraries(braid ${LIBHWLOC_LIBRARY})
    target_link_libraries(spawn_misc ${JEMALLOC_LIBRARY} ${LIBHWLOC_LIBRARY})
    target_link_libraries(equilibrium ${JEMALLOC_LIBRARY} ${LIBHWLOC_LIBRARY})
    target_link_libraries(skynet ${JEMALLOC_LIBRARY} ${LIBHWLOC_LIBRARY})
    target_link_libraries(skynet_prio ${JEMALLOC_LIBRARY} ${LIBHWLOC_LIBRARY})
    target_link_libraries(fib ${JEMALLOC_LIBRARY} ${LIBHWLOC_LIBRARY})
    target_link_libraries(runtime ${LIBURING_LIBRARY} ${LIBHWLOC_LIBRARY})
    target_link_libraries(asio_http_server ${JEMALLOC_LIBRARY} ${LIBURING_LIBRARY} ${LIBHWLOC_LIBRARY})
    target_link_libraries(asio_http_skynet ${JEMALLOC_LIBRARY} ${LIBURING_LIBRARY} ${LIBHWLOC_LIBRARY})
endif(UNIX)

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

add_custom_command(TARGET runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_CURRENT_LIST_DIR}/build
)
