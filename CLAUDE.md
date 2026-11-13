# Info
This is a repo for a C++20 coroutine tasking library.

The top level folder contains the CMake project. Run all CMake commands from the top level folder.

Subfolders:
- /examples - contains all examples of how to use the library
- /tests - contains tests for the library
- /submodules/TooManyCooks - contains the library source code. This is a Git submodule so you can diff or review commits here.

## Building
This project uses CMake configure presets. There are no build presets. Unless the user specifies otherwise, you should use the following:
- clang-linux-debug preset for testing
- clang-linux-relwithdebinfo preset for benchmarking and profiling

## Instructions
After you make any changes, always build and run the tests.
```bash
# Build
cmake --build ./build/clang-linux-debug --target tests
# Run
./build/clang-linux-debug/tests/tests
```
Tests is a Google Test executable so you can apply the `--gtest_filter=` command line parameter if you need to execute a specific test for troubleshooting.

## This Branch - Coroutine Debugger Scripts

This document provides context for maintaining and improving the GDB and LLDB debugger scripts that reconstruct synthetic coroutine stack traces for the TMC library. The scripts only need to work for TMC coroutines.

## Scripts Overview

| Script | Debugger | Purpose |
|--------|----------|---------|
| `coro_backtrace_gdb.py` | GDB | Frame filter (`[async]` frames in `bt`), `std::coroutine_handle` pretty printer, `$get_coro_frame()` / `$get_coro_promise()` |
| `coro_backtrace_lldb.py` | LLDB 22+ | Scripted frame provider (`[synthetic]` frames in `bt`, the SB API and `lldb-dap`) |
| `tests/coro_backtrace/` | both | Regression suite (`run.py`), see Testing |
| `test.gdb` | GDB | Scratch script for the GDB console `bt` command |
| `testmi.gdb` | GDB | Scratch script for GDB MI output (what VS Code's C/C++ extension reads) |
| `test.lldb` | LLDB | Scratch script for LLDB |

This project requires using a debugger. To do this, use a script.
If you need to test other commands, edit the script file and run the bash command again.
The scratch scripts name the binary on their `file` line; edit that line to switch compiler or build type. A binary passed on the command line is replaced by it.

```bash
lldb --batch -s test.lldb          # LLDB
gdb --batch -x test.gdb            # GDB console
gdb --batch -i mi -x testmi.gdb    # GDB MI
```

Unless the user requests otherwise, use the `fib` program for debugging. The program source is at `./examples/fib.cpp`. Debug builds hardcode `n = 3`; optimized builds need an argument (`run 5`). It runs on one thread so that the output is deterministic.

## What Works (verified on x86-64, GDB 17.2, LLDB 22.1.8)

| Build | GDB | LLDB |
|-------|-----|------|
| Clang 19 / 20 / 21 `-O0` | chain and lines correct | chain and lines correct |
| GCC 13 / 14 / 16 `-O0` | chain and lines correct | chain and lines correct |
| Clang 20 / 21 `-O2` | chain and lines correct | chain and lines correct |
| GCC 13 / 14 / 16 `-O2` | chain and lines correct | chain and lines correct in the default mode; some stacks get no async frames (see Known Limitations) |
| Clang 19 `-O2` | no async frames | no async frames |

GCC `-O2` means both forms of the index store: default (generic) tuning and `-mtune=znver3` (see "Find the suspend line").

This table is what the regression suite (`tests/coro_backtrace/run.py`, see Testing) measures, with its own flags and with this repo's preset flags; the exceptions it knows about are the `KNOWN` list in that file. The suite also covers: 8 worker threads (`thread apply all bt` / `bt all`), core files, Intel disassembly flavor, coroutines with more than 256 suspend points, `spawn_many` / `spawn_tuple` / `fork()` (`[fork]`), `ex_braid`, corrupted (cyclic) continuation chains, null and garbage handles in the pretty printer, sourcing the GDB script twice, importing the LLDB script before a target exists, member-function and `this`-capturing lambda coroutines, evaluating an expression on async frames of every thread in LLDB (both frame modes), GDB/MI output, and `lldb-dap` (names, `[fork]`, `subtle` hint, `evaluate`).

## How The Scripts Work

Both scripts do the same three things. Keep them in step when changing one.

### 1. Find the running coroutine

A physical frame is a coroutine body if it has the frame variable: `__coro_frame` (Clang, the frame struct itself) or `frame_ptr` (GCC, a pointer parameter of the `.actor` function). Do **not** use the promise variable (`__promise` / `_Coro_promise`) for this: it is `<optimized out>` in optimized builds, and Clang's ramp function has one too, holding garbage until the promise constructor has run.

- Clang's ramp function also declares `__coro_frame`. It is skipped by symbol name: only `.resume` / `.destroy` / `.cleanup` clones (GCC: `.actor` / `.destroy`) are accepted. Clang <= 20 gives the clones the ramp's `DW_AT_linkage_name`, so the ELF symbol is what tells them apart.
- `frame_ptr` is an ordinary identifier. Both scripts check the type (a struct with `_Coro_resume_fn`) and never fall back to a global lookup.
- GDB only: for a member-function coroutine built with GCC <= 14 that struct has no members in GDB. GCC declares it inside the member function's *declaration*, and GDB does not read the members of a type declared there (GCC 16 declares it in the class). The actor still has a variable per frame member, so the GDB script accepts an empty struct if the function has a `_Coro_resume_index` variable, and reads the index offset from the code: the actor starts by dispatching on the index, so its first `movzwl disp(%reg),%reg` is that load (`_gcc_index_offset_from_code`). The variable's own location cannot be used: it is an expression on a live `frame_ptr`, which a suspended coroutine does not have.
- GDB (`_physical_coro_frame_address`) treats every physical coroutine frame as a root and de-duplicates by coroutine frame address. LLDB (`_running_coroutine`) uses only the innermost one.

### 2. Walk the continuation chain

The coroutine frame starts with the resume and destroy function pointers; the promise follows at 2 pointers rounded up to the promise's alignment (ABI, both compilers). TMC stores the awaiting coroutine in `promise.customizer.continuation` (a `void*`). If `customizer.done_count` is non-null (`fork()`, `spawn_many`, `spawn_tuple`), `continuation` points to a `std::coroutine_handle` instead of being one; those links are shown as `[fork]`.

The coroutine function of a frame is recovered from the function pointers in its header (GDB: destroy pointer, `_CoroFunction`; LLDB: resume pointer, `CoroFnInfo`). The pointer must be the *entry* of a function. In GDB, `gdb.block_for_pc()` is not enough for that: in optimized builds it returns an inlined callee, and GCC nests a lambda's actor inside the enclosing function's block (`_function_block_at_entry`).

The walk stops at a null continuation, a frame that does not validate, a frame already visited, or `MAX_ASYNC_DEPTH`. A coroutine whose promise has no `customizer` (not a TMC task) is shown but not followed.

### 3. Find the suspend line

The suspend index is `__coro_index` (Clang) or `_Coro_resume_index` (GCC; even values, the low bit selects the destroy path). Clang's DWARF says `__coro_index` is 1 byte even when a coroutine has more than 256 suspend points and the member is really 2 bytes (`_read_index` in GDB, `index_width` in LLDB).

Sources for the line, in order of preference:

1. **Clang >= 21 label** (GDB only): `DW_TAG_label` `__coro_resume_N` carries file and line. LLDB has no access to `DW_TAG_label`.
2. **Index store scan** (x86 only): find `mov{b,w} $N, index_offset(%reg)` in the resume function (`_scan_index_stores` in GDB, `CoroFnInfo._build_await_map` in LLDB). AT&T syntax is forced for the scan, the displacement must match exactly, and stores are attributed to the function by ELF symbol because a block's `start..end` spans other functions when GCC splits hot and cold code.
   - **Not every matching store is the coroutine's own.** When the ramp of an awaited child is inlined into the resume function, it writes the child's frame: the initial index, the arguments. The child's frame often has the same layout, so the displacement matches. GCC does this at `-O2`, and far more often with `-fno-exceptions` (one of this repo's preset flags), which leaves the ramp small enough to inline. A foreign store that is taken for a suspend point ends the search for the awaiter early, and the lines of the neighbouring suspend points come out permuted. Both scripts therefore skip stores that are in inlined code, and for GCC every value that is not 2, 4, 6, ... (the actor never stores 0 or an odd value). LLDB asks the block (`GetContainingInlinedBlock`). GDB cannot simply test "not directly in the function's block": for a lambda, GCC describes parts of the actor as code inlined into the enclosing function, and GDB files them under that function. It compares the stores with each other instead: one that is nested inside the function block of another one is in inlined code (`nesting` in `_scan_index_stores`). The base register says nothing; only `%rsp` and `%rip` are excluded.
   - **GCC splits the store in two** when it tunes for CPUs where a 16-bit immediate stalls the decoder: `mov $N,%ecx` ... `mov %cx,index_offset(%reg)`. That includes the default (generic) tuning, so this is what most optimized GCC builds contain. `-march=native` on an AMD CPU (and so this repo's presets on such a machine) gives the single instruction. `_split_store_value` takes N from the nearest earlier instruction that writes the register, going back to the start of the basic block. The load can be dozens of instructions ahead of the store, with unrelated code in between, and across a call when the register is callee-saved (GCC keeps the constant in `%r12` over the callee's ramp).
   - Clang attributes the store to the `co_await` line, and that line is used as it is.
   - GCC: the store's own line is not usable. GCC >= 14 attributes it to the function's closing brace, and in optimized code of any version it can be a line of a header. What carries the `co_await` line is the awaiter's `await_ready` / `await_suspend` around the store: a real call at `-O0`, the call site of inlined code at `-O2` (`_gcc_await_location` in both scripts, for every GCC version). Look forward from the store to the end of the suspend path (an unconditional jump or return, or the next store), then backward to the start of the basic block. Only if no awaiter code is left (`std::suspend_always` folds away completely) fall back: with GCC >= 14 to the next line that is not the brace, otherwise to the store's own line. "The next line that is not the brace" alone is not enough at `-O2`: the optimizer moves the previous statement, or code of an inlined callee, in between.
   - Instructions inside inlined code must be mapped to their call site in the coroutine body (`_location_in_function` / `_awaiter_call_site` in GDB, `_body_location` in LLDB), otherwise the line comes from a library header.
3. **GCC >= 16 label** (GDB only, fallback): `resume.N` has an address but no line. The line at that address is usually right at `-O0` and often wrong at `-O2`.

If none works, GDB prints the frame without a line; LLDB shows the function's first line.

## Compiler Differences (measured)

| Feature | Clang | GCC |
|---------|-------|-----|
| Frame variable | `__coro_frame` (struct) | `frame_ptr` (pointer) |
| Promise | `__promise` variable and frame member; at `-O1`+ the member is renamed and the variable is optimized out | `_Coro_promise` frame member |
| Suspend index | `__coro_index` (declared 1 byte, see above) | `_Coro_resume_index` (2 bytes) |
| Coroutine body symbols | `f.resume`, `f.destroy`, `f.cleanup` | `f.actor`, `f.destroy` |
| Resume-point labels | >= 21: `__coro_resume_N` with file/line (some are dropped at `-O2`); <= 20: none | 16: `resume.N` with address only; 13 / 14: no address |
| Line of the index store | the `co_await` | 13: the `co_await`; >= 14: the closing brace |
| Index store at `-O2` | `movb` / `movw $N, index(%reg)` | generic tuning: `mov $N,%r32` ... `mov %r16, index(%reg)`; AMD tunings: `movw $N, index(%reg)` |
| Frame struct of a member function | in the function | <= 14: inside the function's declaration (GDB sees no members); 16: in the class |
| Multi-line `co_await` | reported at its first line | reported at the last line of the operand |
| Frame debug info at `-O2` | >= 20: present; 19: absent | present |
| Child's ramp inlined into the parent's resume function at `-O2` | not seen in the suite | yes; its stores to the child's frame look like index stores |
| Blocks of a lambda's actor at `-O2` | the function's own | partly described as code inlined into the enclosing function (GDB files it there, LLDB does not) |
| Symmetric transfer at `-O0` | tail call | nested call: resumed parents and finished children stay on the physical stack |

## Debugger Differences

### GDB (`coro_backtrace_gdb.py`)
- `FrameFilter` + `FrameDecorator`; synthetic frames are attached to the physical frame they follow.
- Per-function debug info is cached in `_function_cache` and dropped on `new_objfile` / `clear_objfiles` / `free_objfile`.
- Symbols are read from the function's own block, not with `gdb.lookup_symbol` (which falls through to globals and is slow when it fails).
- The Python API has no minimal-symbol lookup; `_elf_symbol_at` parses `info symbol`.
- The pretty printer must never raise: an exception is printed once per value and floods `bt` in optimized builds.
- Core files work. To make one for a test, note that `gcore` (GDB 17.2) with glibc 2.42+ writes a core without thread stacks: the guard page is inside the stack's mapping, and `gcore` abandons a mapping at the first unreadable page. The suite's `gdb_probe.py` removes the guards first. Cores written by the kernel are not affected.
- MI: synthetic frames have no `level` field (GDB only numbers real frames). VS Code's C/C++ extension 1.35.6 displays them; `.vscode/launch.json` enables `-enable-frame-filters` and loads the script through `miDebuggerArgs`. Check the raw MI output with `testmi.gdb`.

### LLDB (`coro_backtrace_lldb.py`)
- `ScriptedFrameProvider` (LLDB 22+), registered per target: the script must be imported after the target exists (`preRunCommands` in `lldb-dap`, not `initCommands`). It says so when imported too early or on an older LLDB.
- Frames are produced lazily in `get_frame_at_index`, as LLDB asks for them.
- Per-function info is cached by (module UUID, file address), so it survives re-runs and ASLR.
- The resume function is disassembled from its bytes, at an address that has no section (`CoroFnInfo._disassemble`), not with `SBFunction.GetInstructions`. For every instruction with an address operand LLDB looks up a symbol for the comment, and in a running process that lookup can take a second per instruction (LLDB 22.1.8, `DisassemblerLLVMC::SymbolLookup` -> `Function::GetAddressRange`). With GCC 13 `-O2 -march=native` the first backtrace through a coroutine with 270 awaits took more than five minutes; it now takes a quarter of a second. The bytes come from the object file (`SBSection.GetSectionData`), because a core file usually has no code pages; memory is the fallback. Instruction addresses are therefore file addresses, and `_Insn.addr` resolves one again where a block or a line is needed.
- `FRAME_MODE` (env `CORO_BT_FRAME_MODE`, or `script coro_backtrace_lldb.FRAME_MODE = "dict"` after import):
  - `scripted` (default): `ScriptedFrame` objects (`CoroFrame`). The script supplies the name and the exact line, and the frames are artificial, which `lldb-dap` reports with the `subtle` hint (greyed out in VS Code). Names carry a `[fork]` prefix where applicable; this shows in the SB API and `lldb-dap`, not in console `bt`, which formats the name from the symbol context. For GCC the name is the coroutine's own (`fib`, `main::'lambda'(unsigned long)::operator()`), derived from the ELF symbol by `_gcc_coroutine_name`, because the actor's demangled symbol is `fib(_ZL3fibm.Frame*) (.actor)` and its debug-info name is no better in optimized builds. Clang names are left as LLDB shows them for real frames.
  - `dict`: frames are `{"idx", "pc"}` and LLDB derives function, name and line from the pc. For GCC the script therefore looks for a pc that is not inside inlined code and whose own line entry is the wanted line (`_find_clean_pc`). Kept as a fallback that avoids LLDB's `ScriptedFrame` code entirely.
- `CoroFrame.get_symbol_context()` always sets the block to the coroutine function's own top-level block. Left unset, LLDB resolves the block itself, at `pc - 1`, and in optimized code that is often a function inlined into the coroutine. The frame then takes that function's identity: its name in `bt`, its `this` in expressions.
- **LLDB bugs the script works around** (all seen on LLDB 22.1.8). The last two only show up with member-function coroutines or optimized multi-threaded builds, so `fib` alone does not cover them.
  - **Null variable list** (segfault; unfixed in 22.x and 23.x as of 23.1.5; fixed on `main` by llvm/llvm-project#239795, merged 2026-11-06): if `ScriptedFrame.get_variables()` returns `None` (the base class default), `ScriptedFrame::PopulateVariableListFromInterface()` leaves the frame's variable list null and `ClangExpressionSourceCode::AddLocalVariableDecls()` dereferences it. `expression` and `lldb-dap` Watch / Debug Console then crash on any async frame. `CoroFrame.get_variables()` returns an empty `lldb.SBValueList()` to avoid it. Keep its `filters=None` default: LLDB calls it without arguments, and a `TypeError` there brings the crash back. A register context makes no difference.
  - **Null `this`** (segfault; unfixed in 22.x and 23.x; fixed on `main`): when the frame's function is a C++ instance method, expression evaluation asks the frame for `this`, and `ClangUserExpression::GetCppObjectPointer()` dereferences the answer before checking it. This hits member-function coroutines (with Clang even at `-O0`) and, without the block fix above, any frame whose `pc - 1` lies in an inlined method. `CoroFrame.get_value_for_variable_expression()` answers `this` with a null `void*` and an error; LLDB then warns "`this' is not accessible (substituting 0)" and evaluates the expression.
  - **Entry values** (LLDB aborts with `std::system_error: Resource deadlock avoided`; 22.x only, fixed in 23.x; both frame modes): reading a variable whose location is `DW_OP_entry_value` makes LLDB ask the thread for the caller's frame, which re-enters the frame list that is locked while the provider runs. GCC `-O1`+ describes `frame_ptr` that way wherever its register has been reused. `_reads_entry_value` checks for it before `_running_coroutine` reads the variable. The SB API cannot show a variable's location without evaluating it, so the check parses `image lookup -v -a`, run with a target-only execution context (with a thread in the context the command re-enters the list too). The address must come from the pc *register*, not `frame.GetPC()`: the two differ for a frame that has inlined callees at that point, and LLDB picks the location by the register. The abort depends on where each thread happens to be stopped: before the check, about a third of `bt all` runs on an 8-thread GCC `-O2` build died.
- **LLDB deadlock with core files** (LLDB 22.1.8; not worked around, and the script cannot): after a core is loaded and the script imported, LLDB's event thread builds the frames of the core's threads, which constructs the frame provider on that thread. If another thread asks for frames at that moment, it holds the API lock and waits for the frame list, while the event thread holds the frame list and waits for the API lock (in `ScriptedFrameProvider.__init__`, at `SBThread.IsValid()`). From the SB API right after `LoadCore` and the import it happened every time; from a batch script (`target create --core`, `command script import`, `bt`) in about 1 run of 10. Waiting a moment after the import avoids it (`lldb_probe.py` does), and interactive use is not affected in practice. This is most likely the hang on a core file that an early review of the script blamed on producing frames eagerly.

## Known Limitations

- **x86 only for the store scan.** On other architectures GDB gets lines only from labels (Clang >= 21; GCC >= 16 approximately) and LLDB shows the function's first line. AArch64 emits `mov w8, #N; strb w8, [x0, #off]`; nothing has been run on AArch64.
- **Clang <= 19 optimized builds** have no coroutine frame debug info, so there are no async frames.
- **GCC `-O2` lines rest on heuristics** (instruction patterns and the call sites of inlined awaiter code). In the suite they are all right in GDB and in LLDB's default mode. Code that defeats them gets no line in GDB and the function's first line in LLDB. LLDB's `dict` mode additionally shows the closing brace when every instruction of the `co_await` line belongs to inlined calls: a multi-line `co_await`, the `co_await` in `main`'s lambda (GCC 14, and GCC 16 with AMD tuning or with `-fno-exceptions`), a `spawn_tuple` (GCC 14).
- **GCC `-O2` + LLDB**: a stack gets no async frames when `frame_ptr` cannot be read at the stop location. Two causes: it lives in a caller-saved register across a call, which LLDB will not read in a non-top frame (GDB reads it and shows the frames); or it is described only as an entry value, which the script must not read (see the LLDB bugs above).
- **GCC `-O2` + GDB**: a physical coroutine frame whose `frame_ptr` is `<optimized out>` at its pc cannot start a chain. Seen for a lambda at the point where it calls an eagerly started non-TMC coroutine: the frames above it are right, the ones that await the lambda are missing.
- **GCC `-O0`**: the physical frames of resumed parents and finished children appear below the async frames (no tail calls). A finished child's coroutine frame is already freed; its stale chain is not shown again only because its continuation is already on the stack.
- **A forked child that runs before its parent awaits it** has no parent to show: the continuation slot is null until then.
- **Non-TMC coroutines** end the chain. With Clang at `-O2`, one that suspended inside its ramp (eager start) shows its first line in LLDB, and no line in GDB with Clang 20, because that index store is in the ramp and not in the resume clone. (GDB with Clang 21 has the label.)
- **LLDB + core files**: a batch script that asks for a backtrace right after loading the core and importing the script can deadlock inside LLDB (see the LLDB bugs above).
- **LLDB recursion**: all async frames in the same function get equal frame IDs, so the SB API and `lldb-dap` show the first of them in place of the others (e.g. `fib` at line 43 three times where one is really `[fork] fib` at line 44). Console `bt` is right. A frame ID is the CFA plus the frame's function block, and LLDB 22 / 23 hardcode a CFA of 0 for a `ScriptedFrame`, so the script cannot tell them apart. LLDB `main` asks the frame for a CFA (`get_cfa()`, default: the frame id; llvm/llvm-project#223834), which should make the IDs distinct. Not tested.
- **LLDB console `bt`** formats the name from the symbol context, not from the script. It never shows `[fork]`, and GCC names are LLDB's own: `fib(frame_ptr=<unavailable>)`, and at `-O2` `(frame_ptr=<unavailable>)::operator()(main(int, char **)::(unnamed struct)::_ZZ4mainENKUlmE_clEm.Frame *)` for the lambda. The SB API and `lldb-dap` get the script's names.
- **LLDB `dict` mode** has no `[fork]` marker anywhere, and `lldb-dap` does not grey the async frames out.
- Locals and arguments of async frames are not reconstructed in either debugger. In LLDB, an expression evaluated on an async frame of a member-function coroutine warns that `this` is not accessible.

## Build Commands

There are no build presets; build by directory.

```bash
cmake --preset clang-linux-debug
cmake --build ./build/clang-linux-debug --target fib
```

The same works for `gcc-linux-debug`, `clang-linux-relwithdebinfo` and `gcc-linux-relwithdebinfo`.

## Testing

`tests/coro_backtrace/run.py` is the regression suite; `tests/coro_backtrace/README.md` describes it. It builds three test programs with every compiler on `PATH` (`-O0`, `-O2`, and for GCC also `-O2 -mtune=znver3`), runs them in GDB, in LLDB (both frame modes) and through `lldb-dap`, and compares the async frames with the `co_await` lines marked in the sources. Run it after changing either script:

```bash
tests/coro_backtrace/run.py                                           # everything, a few minutes
tests/coro_backtrace/run.py --compilers g++-14 --debuggers gdb --tests awaits
```

- `FAIL` names the stop, the kind of failure and the log. The observations are JSON files next to the log.
- `XFAIL` is a limitation listed in `KNOWN` in `run.py`, with its reason. Do not add an entry to make a failure go away before you know why the scripts cannot get it right: most of what looked like compiler limitations while the suite was written turned out to be script bugs (the split index store, member functions with GCC <= 14, constants kept in callee-saved registers, stores of an inlined child's ramp).
- The suite builds header-only with its own flags. This repo's presets add `-march=native -fno-exceptions`; to check those, add `--cxxflags="-march=native -fno-exceptions" --build-dir build/coro_backtrace_native`. `KNOWN` is recorded for the default flags, which leave exceptions enabled. The two preset flags were also run one at a time. GDB and LLDB's default mode give the default-flag results in every combination. Two `dict`-mode results differ: GCC 16 `O2` `threads` fails the way `O2-zen` is known to (with either flag), and GCC 14 `O2` `awaits` is an `XPASS` with `-march=native` (fewer wrong lines than listed).
- When a new case is worth keeping, add it to a program in `tests/coro_backtrace/programs/` (an anchor comment on the `co_await` line and an entry in the tables at the top of `run.py`).

For a quick look by hand, use the scratch scripts with `fib`. With a breakpoint at `examples/fib.cpp:44` the async frames must be the `co_await` lines:

- `fib` parents at `fib.cpp:43` (plain await) or `fib.cpp:44` (`[fork]`), when the stop is deeper than the first `fib`
- `top_fib` at `fib.cpp:53`
- the lambda in `main` at `fib.cpp:80`
- `tmc::detail::client_main_awaiter` at the `co_await` in `ex_cpu.ipp` (first line of the expression with Clang, last line with GCC)

Line results depend on the compiler version, so say which versions you tested. Several compiler versions are installed side by side (`clang++-19/20/21`, `g++-13/14/16`).
