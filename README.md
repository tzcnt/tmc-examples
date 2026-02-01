![x64-linux-gcc](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-gcc.yml/badge.svg) ![x64-linux-clang](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang.yml/badge.svg) ![x64-windows-clang-cl](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-windows-clang-cl.yml/badge.svg) ![arm64-macos-clang](https://github.com/tzcnt/tmc-examples/actions/workflows/arm64-macos-clang.yml/badge.svg)

![AddressSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-asan.yml/badge.svg) ![ThreadSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-tsan.yml/badge.svg) ![UndefinedBehaviorSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-ubsan.yml/badge.svg)

## tmc-examples Coroutine Backtrace Development Branch

This branch contains the `coro_backtrace_lldb.py` and `coro_backtrace_gdb.py` scripts.
These scripts enable you to view synthetic async stacks in your debugger.

These scripts are under active development. They are also 100% AI-generated, so no guarantees are made about the accuracy of the output at this point. The AGENTS.md in this branch has been updated to provide context to LLMs about the current state of development, if you want to try hacking on them yourself.

### LLDB Script Status: It works on my machine :man_shrugging:
Requirements:
- LLDB 22 (this is a pre-release LLDB version)
- Debug (-O0 -g) executable created by Clang or GCC

To load the script, execute `command script import ./coro_backtrace_lldb.py` inside LLDB. See the `test.lldb` file for usage example.

IDE integration is provided in this repo via the `launch.json` file for VS Code with the LLDB DAP plugin. This also requires the `lldb-dap` version 22 executable to be available. This will display the synthetic async stack frames in your Call Stack window.

### GDB Script Status: Works in CLI only
Requirements:
- Tested with GDB 16. IDK the lowest supported version
- Debug (-O0 -g) executable created by Clang or GCC

To load the script, execute `source coro_backtrace_gdb.py` inside GDB. See the `test.gdb` file for usage example.

IDE integration for VSCode (with C++ tools debugger extension) is currently broken, as GDB does not allow providing the `level` field for synthetic frames, but VSCode expects a `level` for every frame.

## tmc-examples
This repository contains examples of how to use the TooManyCooks asynchronous runtime. The main TMC project repository can be found at:
https://github.com/tzcnt/TooManyCooks/

Some of the examples also use other libraries in the TMC ecosystem:
https://github.com/tzcnt/tmc-asio/

This repository uses CMake to download and configure the TMC libraries, either as CPM packages (by default), or as git submodules (optionally). It provides a `CMakePresets.json` with configurations for Linux/Windows/MacOS and Clang/GCC/MSVC.

### Recommended IDE setup (VSCode)
Requirements:
- VSCode
- C/C++ extension
- CMake Tools extension
- LLDB DAP extension

After you install the CMake Tools extension, be sure to set the setting "Cmake > Options: Status Bar Visibility" to "visible" or "compact" in order to be able to switch presets, build, and run targets.

A `launch.json` is included which provides one-click debugging in LLDB or GDB for the currently selected CMake build target.

Other IDEs with CMakePresets integration, such as Visual Studio, will also work.

### Building from the CLI
Choose a value for `PRESET` from `CMakePresets.json` that is appropriate for your system.
```bash
PRESET=clang-linux-release
cmake --preset $PRESET .
cmake --build ./build/$PRESET --target all
cd ./build/$PRESET
./tests/tests
```

For a minimal project template to setup TMC for your own uses, see :octocat: [tmc-hello-world](https://github.com/tzcnt/tmc-hello-world).

### Supported Compilers
All 3 major compilers are fully supported, but Clang is the recommended compiler, as it has the best coroutine codegen and the most functional HALO implementation.

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

### License
Although the TMC libraries are licensed under the Boost Software License 1.0, the example code in this repository is public domain ("The Unlicense"), and may be copied or modified without attribution.
