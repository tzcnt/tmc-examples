# Regression suite for the coroutine backtrace scripts

Checks what `coro_backtrace_gdb.py` and `coro_backtrace_lldb.py` show against the `co_await` sites of three small test programs, for every compiler it finds on `PATH`.

```bash
tests/coro_backtrace/run.py                       # everything
tests/coro_backtrace/run.py --compilers g++-14    # one compiler
tests/coro_backtrace/run.py --debuggers gdb --tests awaits,member
tests/coro_backtrace/run.py --help
```

The exit code is 0 if nothing failed. This is not part of the CMake build or of the `tests` executable: it needs debuggers, and several compilers at once.

## Requirements

- Linux, x86-64
- Python 3.8 or newer
- Any of `clang++`, `clang++-N`, `g++`, `g++-N` on `PATH`. Two names for the same compiler version are run once.
- GDB, and/or LLDB 22 or newer. `lldb-dap` is optional. A debugger that is missing is skipped.
- The TooManyCooks headers: `submodules/TooManyCooks/include` if it exists, otherwise a CPM checkout under `build/`, or `--tmc-include`.

Binaries, logs and observations go to `build/coro_backtrace/` (`--build-dir`).

## What is built

Each program in `programs/` is built once per compiler and flavor, header-only, with `-std=c++20 -g`:

| Flavor | Flags | Compilers |
|--------|-------|-----------|
| `O0` | `-O0` | all |
| `O2` | `-O2 -DNDEBUG` | all |
| `O2-zen` | `-O2 -DNDEBUG -mtune=znver3` | GCC |

`O2` is what most users build. With GCC's default (generic) tuning the store of the suspend index is split into two instructions (`mov $N,%ecx` ... `mov %cx,index(%reg)`). `O2-zen` keeps the single instruction (`movw $N,index(%reg)`), which is also what `-march=native` gives on an AMD CPU, and this repository's presets use `-march=native`. The scripts have to read both forms. Use `--cxxflags` to try other flags, with a separate `--build-dir`.

| Program | What it has |
|---------|-------------|
| `awaits.cpp` | Sequential awaits, loops, branches, a multi-line `co_await`, `co_await std::move(x)`, two awaits on one line, `spawn_tuple`, `spawn_many`, `fork()`, more than 256 suspend points, an await resumed by an `ex_braid`, a coroutine type that is not a TMC task |
| `member.cpp` | Member-function coroutines and a lambda capturing `this` |
| `threads.cpp` | Recursive fork/join on 8 worker threads |

## What is checked

| Test | Debuggers | Check |
|------|-----------|-------|
| `awaits` | GDB, LLDB | At every `stop_here(tag)`: the async frames are exactly the expected chain. File, line, function name, `[fork]` |
| `member` | GDB, LLDB | The same for `member.cpp`. LLDB also evaluates an expression on every async frame |
| `edge` | GDB, LLDB | `member` again. GDB: script sourced twice, Intel disassembly flavor. LLDB: script imported once before the target exists, Intel flavor |
| `cycle` | GDB, LLDB | `O0` only. The continuation chain is made cyclic in memory; the walk must end |
| `core` | GDB, LLDB | The async stack read from a core file equals the one of the live process |
| `printer` | GDB | `std::coroutine_handle` pretty printer on a null, a live and a garbage handle; `$get_coro_promise` |
| `mi` | GDB | The async frames are in the MI `-stack-list-frames` answer (what VS Code's C/C++ extension reads) |
| `threads` | GDB, LLDB | Every thread's async stack is a plausible one. LLDB: an expression on async frames of every thread, and several runs per build, because some LLDB failures depend on where the threads happen to be stopped |
| `dap` | lldb-dap | A `stackTrace` request shows the async frames with the script's names, `[fork]` and the `subtle` hint (grey in VS Code); `evaluate` works on an async frame as a Watch and in the Debug Console |

LLDB tests run once per frame mode (`scripted`, `dict`); `dap` runs in the default mode only.

No test may print a Python exception, crash the debugger, or time out.

## How the expectations work

The debugger side only observes: `gdb_probe.py`, `lldb_probe.py` and `dap_probe.py` run inside (or drive) the debugger and write what they see as JSON. `run.py` judges it.

Expected lines are not written down anywhere. A `// @name` comment in a program names the line of a `co_await`, and the tables at the top of `run.py` (`AWAITS`, `MEMBER`, `THREADS_*`) say which chain of names leads to each stop. So the programs can be edited freely as long as each anchor stays on the line of its `co_await`. For a `co_await` expression that spans lines, `@name` marks its first and `@name_end` its last line, and any line in between is accepted (Clang reports the first, GCC the last).

To add a case: add the code and a `stop_here(tag)` with a new tag to a program, put anchors on the `co_await` lines, and add the tag to the tables.

## Results

| Status | Meaning |
|--------|---------|
| `PASS` | Everything as expected |
| `XFAIL` | Wrong in exactly the way listed in `KNOWN` in `run.py`: a limitation of a compiler's debug info or of a debugger, not something the scripts can fix. The reason is printed |
| `XPASS` | A listed limitation did not show (newer compiler or debugger). Not a failure; update `KNOWN` |
| `FAIL` | Anything else. The report names the stops and the kind of failure (`no-async`, `line`, `chain`, `name`, `fork`, `expr`, `crash`, `timeout`, `python-error`, ...) and where the log and the observations are |

`KNOWN` is the list of limitations that were measured, each with its reason. It was recorded with GDB 17.2, LLDB 22.1.8, Clang 19 / 20 / 21 and GCC 13 / 14 / 16, and with the flags above. A compiler version that is not listed there has no expected failures.

Other flags (`--cxxflags`) move the limitations of LLDB's `dict` mode around, because they depend on what the optimizer inlines. The default flags leave exceptions enabled. This repository's presets add `-march=native -fno-exceptions`; with those flags, together or one at a time (on an AMD machine), two `dict` results differ from `KNOWN`: GCC 16 `O2` `threads` fails the way `O2-zen` is listed (with either flag), and GCC 14 `O2` `awaits` is an `XPASS` with `-march=native`. GDB and LLDB's default mode give the same results as with the default flags.

## Two workarounds in the probes

- **GDB core files**: glibc 2.42+ keeps the guard page of a thread stack inside the stack's mapping, and `gcore` (GDB 17.2) abandons a mapping at the first page it cannot read, so the core has no thread stacks. `gdb_probe.py` removes the guards (`madvise`) before it calls `gcore`. Cores written by the kernel are not affected.
- **LLDB core files**: asking for frames right after loading a core and importing the script can deadlock inside LLDB 22 (see Known Limitations in `CLAUDE.md`). `lldb_probe.py` waits two seconds first.

## Not covered

- Architectures other than x86-64
- VS Code itself: only the protocols its extensions use (GDB/MI and DAP)
- LLDB older than 22 (the script only prints a message there)
