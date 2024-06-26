## tmc-examples
This repository contains examples of how to use the TooManyCooks asynchronous runtime. The main TMC project repository can be found at:
https://github.com/tzcnt/TooManyCooks/

Some of the examples also use other libraries in the TMC ecosystem:
https://github.com/tzcnt/tmc-asio/

Although the TMC libraries are licensed under the Boost Software License 1.0, the example code in this repository is public domain ("The Unlicense"), and may be copied or modified without attribution.

This repository uses CMake to download and configure the TMC libraries, either as CPM packages (by default), or as git submodules (optionally). It provides a CMakePresets.json with configurations for common Linux and Windows environments.

### Supported Compilers
Linux:
- Clang 16 or newer
- GCC 12.3 or newer

Windows:
- Clang 16 or newer (via clang-cl.exe)
- MSVC latest version (???)

### Supported Hardware
- x86_64 with support for POPCNT / LZCNT
- AArch64
