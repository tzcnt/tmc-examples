![x64-linux-gcc](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-gcc.yml/badge.svg) ![x64-linux-clang](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang.yml/badge.svg) ![x64-windows-clang-cl](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-windows-clang-cl.yml/badge.svg) ![arm64-macos-clang](https://github.com/tzcnt/tmc-examples/actions/workflows/arm64-macos-clang.yml/badge.svg)

![AddressSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-asan.yml/badge.svg) ![ThreadSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-tsan.yml/badge.svg) ![UndefinedBehaviorSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-ubsan.yml/badge.svg)

## tmc-examples
This repository contains examples of how to use the TooManyCooks asynchronous runtime. The main TMC project repository can be found at:
https://github.com/tzcnt/TooManyCooks/

Some of the examples also use other libraries in the TMC ecosystem:
https://github.com/tzcnt/tmc-asio/

This repository uses CMake to download and configure the TMC libraries, either as CPM packages (by default), or as git submodules (optionally). It provides a CMakePresets.json with configurations for common Linux and Windows environments.

For a minimal project template to setup TMC for your own uses, see https://github.com/tzcnt/tmc-hello-world.

Although the TMC libraries are licensed under the Boost Software License 1.0, the example code in this repository is public domain ("The Unlicense"), and may be copied or modified without attribution.

### Supported Compilers
Linux:
- Clang 17 or newer
- GCC 14 or newer

Windows:
- Clang 17 or newer (via clang-cl.exe)
- MSVC Build Tools v145 (Visual Studio 2026 Insiders) or newer due to [this bug](https://developercommunity.visualstudio.com/t/Incorrect-code-generation-for-symmetric/1659260?scope=follow&viewtype=all) which exists in prior versions

MacOS:
- Apple Clang based on Clang 17 or newer with -fexperimental-library

### Supported Hardware
- x86 (32- or 64-bit)
- AArch64
