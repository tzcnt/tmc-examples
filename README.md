![x64-linux-gcc](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-gcc.yml/badge.svg) ![x64-linux-clang](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang.yml/badge.svg) ![x64-windows-clang-cl](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-windows-clang-cl.yml/badge.svg)

## tmc-examples
This repository contains examples of how to use the TooManyCooks asynchronous runtime. The main TMC project repository can be found at:
https://github.com/tzcnt/TooManyCooks/

Some of the examples also use other libraries in the TMC ecosystem:
https://github.com/tzcnt/tmc-asio/

Although the TMC libraries are licensed under the Boost Software License 1.0, the example code in this repository is public domain ("The Unlicense"), and may be copied or modified without attribution.

This repository uses CMake to download and configure the TMC libraries, either as CPM packages (by default), or as git submodules (optionally). It provides a CMakePresets.json with configurations for common Linux and Windows environments.

### Supported Compilers
Linux:
- Clang 17 or newer
- GCC 14 or newer

Windows:
- Clang 17 or newer (via clang-cl.exe)
- ~~MSVC 19.42.34436~~

MSVC has an open bug with symmetric transfer and final awaiters that destroy the coroutine frame. The code will compile but crashes at runtime. This bug has been open since 2022 and they just can't seem to fix it 🤔. ([bug link](https://developercommunity.visualstudio.com/t/Incorrect-code-generation-for-symmetric/1659260?scope=follow&viewtype=all))

### Supported Hardware
- x86_64 with support for POPCNT / LZCNT
- AArch64
