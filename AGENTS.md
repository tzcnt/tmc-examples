# AGENTS.md - Coroutine Debugger Scripts

This document provides context for maintaining and improving the GDB and LLDB debugger scripts that reconstruct synthetic coroutine stack traces for the TMC library.

## Scripts Overview

| Script | Debugger | Purpose |
|--------|----------|---------|
| `coro_backtrace_gdb.py` | GDB | Frame filter and pretty printer for coroutine stacks |
| `coro_backtrace_lldb.py` | LLDB | Synthetic frame provider for coroutine stacks |
| `test.gdb` | GDB | Test script for GDB console `bt` command |
| `testmi.gdb` | GDB | Test script for GDB MI mode debugging |
| `test.lldb` | LLDB | Test script for LLDB debugging |

This project requires using a debugger. To do this, use a script.
If you need to test other commands, edit the script file and run the bash command again.

## Script Usage - LLDB
For LLDB there is a single script. To run:
```bash
lldb --batch -s test.lldb
```

## Script Usage - GDB
For GDB there are 2 scripts.
For general development:
```bash
gdb --batch -x test.gdb
```

To debug issues with MI mode (to test fixes for Blocker 1):
```bash
gdb --batch -i mi -x testmi.gdb
```

## Compiler Differences: GCC vs Clang

| Feature | Clang | GCC |
|---------|-------|-----|
| Promise variable | `__promise` | `_Coro_promise` |
| Coro frame variable | `__coro_frame` | `frame_ptr` (pointer type) |
| Coro index field | `__coro_index` (1 byte) | `_Coro_resume_index` (2 bytes) |
| Assembly pattern | `movb` | `movw` |
| Promise location | Separate symbol | Embedded as field in frame struct |
| Debug info for suspend points | Accurate | Points to function closing brace (requires backward scanning) |

### Key Implementation Notes

- **GCC frame discovery**: For GCC, `_Coro_promise` is embedded as a field within the frame struct, not a separate symbol. Symbol lookup may fail; fallback to searching frame type fields.
- **GCC line number resolution**: GCC's debug info often points suspend code to the closing brace. The scripts scan disassembly backwards to find meaningful line info, skipping the `func_end_line`.
- **TMC continuation access**: TMC stores continuation in `promise.customizer.continuation` as `void*`, not `promise["continuation"]`. Check `done_count` for indirection.

## Debugger Differences: LLDB vs GDB

### LLDB (`coro_backtrace_lldb.py`)
- Uses `ScriptedFrameProvider` API (requires LLDB 22+)
- Disassembly scanning via `_build_await_point_map()` and `_get_await_point_pc()` maps coro_index to PC
- For GCC, uses `_find_await_point_pc_gcc()` for backward scanning (similar to GDB's `_find_await_sal_backwards()`)
- Uses `_get_function_end_line_gcc()` to detect closing brace line (equivalent to GDB's `func_end_line` calculation)
- Full async stack support in VS Code via `lldb-dap` configuration
- Marks forked continuations with `[fork]` prefix when `done_count != 0`
- LLDB cannot read DW_TAG_label debug info, so it cannot use `__coro_resume_N` labels (uses disassembly scanning instead)

### GDB (`coro_backtrace_gdb.py`)
- Uses `FrameDecorator` and `FrameFilter` APIs
- Synthetic frames work correctly in console mode (`bt` command)
- **MI mode has issues** (see Blockers section)

## Blockers

### Blocker 1: GDB Frame Filters Don't Work in MI Mode

**Problem**: `FrameDecorator` synthetic frames lack the `level` field required by MI parsers (`-stack-list-frames`), causing crashes/errors in VS Code.

**Root cause**: GDB calculates frame levels based on underlying `gdb.Frame` objects, which synthetic frames lack. `gdb.Frame` API does not offer a `level()` function which VS Code requires. This cannot be fixed - external bug report must be filed to VS Code / GDB.

**Workaround**: The `-enable-frame-filters` command is commented out in launch.json. For reliable async stack viewing:
- Use console `bt` command in Debug Console
- Or use the LLDB debug configuration (`lldb-dap`)

## Workarounds

### Workaround 1: Finding the suspend point of the calling frame (Clang + LLDB)

**Problem**: Clang emits labels for each suspend point. These labels can be found in the DWARF debug info as DW_TAG_label with DW_AT_name `"__coro_resume_" + index`.
GDB can read these labels. LLDB cannot read the labels, since it does not load this debug info.

**Fix**: Scan the resume function's disassembly to find where `__coro_index` is stored before each suspend point. The `_build_await_point_map()` method looks for `movb $N, offset(%reg)` instructions where the offset matches the `__coro_index` field offset. This maps each index value N to the PC of the store instruction, which corresponds to the await point source line.

### Workaround 2: Finding the suspend point of the calling frame (GCC)

**Problem**: GCC's suspend point line information is inaccurate. It often points to the end of the function.

**Fix**: Similar to Workaround 1, but with additional backward scanning. The `_build_await_point_map_gcc()` method finds `movw $N, offset(%reg)` instructions (GCC uses 2-byte index). Since the line info at these instructions often points to the closing brace, `_find_await_sal_backwards()` scans backwards up to 50 instructions to find an instruction with meaningful line info (line number != `func_end_line`).

### Workaround 3: GCC Frame Filter Duplication

**Problem**: GCC may show multiple async frames for the same coroutine at different stack positions.

**Fix**: Added `generated_async_frames` flag to `CppCoroutineFrameFilter.filter` to generate async frames only once from the topmost coroutine frame.

### Workaround 4: GCC Promise Pointer Dereference

**Problem**: In GCC's stack unwinding context, the `promise` value may be a pointer.

**Fix**: Added dereferencing logic in `_get_continuation` to handle pointer case.

## Build Commands

```bash
# Configure and build with Clang
cmake --preset clang-linux-debug
cmake --build --preset clang-linux-debug

# Configure and build with GCC
cmake --preset gcc-linux-debug
cmake --build --preset gcc-linux-debug
```

Unless the user requests otherwise, use the `fib` program for debugging.
The program source is at `./examples/fib.cpp`.

## Testing

Test both compiler outputs to ensure compatibility:
```bash
# Test LLDB with Clang binary
lldb --batch -s test.lldb ./build/clang-linux-debug/fib

# Test LLDB with GCC binary
lldb --batch -s test.lldb ./build/gcc-linux-debug/fib

# Test GDB with Clang binary
gdb --batch -i mi -x testmi.gdb ./build/clang-linux-debug/fib

# Test GDB with GCC binary
gdb --batch -i mi -x testmi.gdb ./build/gcc-linux-debug/fib
```
