![x64-linux-gcc](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-gcc.yml/badge.svg) ![x64-linux-clang](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang.yml/badge.svg) ![x64-windows-clang-cl](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-windows-clang-cl.yml/badge.svg) ![arm64-macos-clang](https://github.com/tzcnt/tmc-examples/actions/workflows/arm64-macos-clang.yml/badge.svg)

![AddressSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-asan.yml/badge.svg) ![ThreadSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-tsan.yml/badge.svg) ![UndefinedBehaviorSanitizer](https://github.com/tzcnt/tmc-examples/actions/workflows/x64-linux-clang-ubsan.yml/badge.svg)

## tmc-examples Coroutine Backtrace Development Branch

[<img src="https://fleetcode.com/img/backtrace.png">](https://fleetcode.com/img/backtrace.png)

This branch contains the `coro_backtrace_lldb.py` and `coro_backtrace_gdb.py` scripts.
These scripts enable you to view synthetic async stacks in your debugger.

These scripts are under active development. They are also 100% AI-generated, so no guarantees are made about the accuracy of the output at this point. The AGENTS.md in this branch has been updated to provide context to LLMs about the current state of development, if you want to try hacking on them yourself.

The scripts currently only support x86-64 architecture, as they need to parse assembly to work around limitations of the available debug info. AArch64 support will be coming soon(tm).

If you find any problems while testing these scripts, please open an issue on this repository (tmc-examples). PRs are also welcome.

### Requirements (both scripts)
- An executable built by Clang or GCC with debug info (`-g`). Tested with Clang 19, 20, 21 and GCC 13, 14, 16.
- Debug (`-O0`) builds give exact line numbers.
- Optimized builds (e.g. `RelWithDebInfo`) also work, with two caveats:
  - Clang 19 and older don't emit the coroutine frame debug info when optimizing, so you get no async frames. Clang 20+ is fine.
  - With GCC, LLDB sometimes cannot find the async stack of a thread (GDB can).

Awaits that go through `fork()`, `spawn_many()` or `spawn_tuple()` are part of the async stack too. Locals of the async frames are not available.

A regression suite for both scripts is in `tests/coro_backtrace/` (`tests/coro_backtrace/run.py`, see the README there). It needs GDB and/or LLDB and tests every `clang++` / `g++` version it finds.

### LLDB Script Status: It works on my machine :man_shrugging:
Requirements:
- LLDB 22 or newer

To load the script, execute `command script import ./coro_backtrace_lldb.py` inside LLDB, after the target has been created. See the `test.lldb` file for usage example.

IDE integration is provided in this repo via the `launch.json` file for VS Code with the LLDB DAP plugin. This also requires the `lldb-dap` version 22 executable to be available. This will display the synthetic async stack frames in your Call Stack window.

Known issues:
- If the same coroutine function appears more than once in the async stack (recursion), VS Code shows the first of those frames in place of the others. `bt` in the console is correct. This is a limitation of LLDB 22 and 23; [llvm-project#223834](https://github.com/llvm/llvm-project/pull/223834), which is not in a release yet, should remove it.
- The script works around three LLDB bugs that the async frames run into:
  - Two crashes when evaluating an expression (including a VS Code Watch) while an async frame is selected. Both are fixed on LLVM `main` (the first by [llvm-project#239795](https://github.com/llvm/llvm-project/pull/239795)) but not in any release as of LLDB 23.1.5. For a coroutine that is a member function, LLDB warns that `this` is not accessible and then evaluates the expression.
  - An abort with `Resource deadlock avoided` while printing a backtrace of a GCC optimized build. This one is fixed in LLDB 23.
- With a core file, a batch script that asks for a backtrace immediately after loading the core and importing the script can hang inside LLDB (about 1 run in 10 here). Typing the commands by hand is not affected.
- If LLDB still crashes with an async frame selected, set the environment variable `CORO_BT_FRAME_MODE=dict` before starting LLDB (or run `script coro_backtrace_lldb.FRAME_MODE = "dict"` after loading the script) to make the script hand LLDB plain frames instead. That mode loses the `[fork]` marker, and some GCC optimized-build line numbers.

In VS Code, async frames are greyed out and forked awaits are marked `[fork]`. The console `bt` marks async frames `[synthetic]` but does not show `[fork]`, and it prints GCC coroutines under the compiler's internal names (`fib(frame_ptr=<unavailable>)`), where VS Code shows `fib`.

### GDB Script Status: Works in CLI and VS Code
Requirements:
- Tested with GDB 17.2. IDK the lowest supported version

To load the script, execute `source coro_backtrace_gdb.py` inside GDB. See the `test.gdb` file for usage example. Async frames are marked `[async]`, and `[fork] [async]` for forked awaits.

The script also pretty-prints `std::coroutine_handle` values, and provides `p *$get_coro_frame(handle)` and `p *$get_coro_promise(handle)`.

IDE integration is provided in this repo via the `launch.json` file for VS Code with the C/C++ extension (tested with version 1.35.6). GDB does not provide the `level` field for synthetic frames, and earlier versions of the extension expected one for every frame.

## tmc-examples
This repository contains examples of how to use the TooManyCooks asynchronous runtime. The main library project repository can be found at:
https://github.com/tzcnt/TooManyCooks/

Some of the examples also make use of the optional [Asio integration](https://github.com/tzcnt/TooManyCooks/tree/main/include/tmc/asio).

This repository uses CMake to download and configure TooManyCooks, either as a CPM package (by default), or as git submodules (optionally). It provides a `CMakePresets.json` with configurations for Linux/Windows/MacOS and Clang/GCC/MSVC.

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
