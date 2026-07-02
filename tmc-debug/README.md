# TooManyCooks Coroutine Debugging (VS Code)

Shows the **async coroutine call stack** and each suspended coroutine's **locals** in VS Code,
on the **stock** debug adapters — **no patched adapter, no fork**:

- **`cppdbg-tmc`** — GDB, via Microsoft's `cppdbg` / `OpenDebugAD7`.
- **`lldb-dap-tmc`** — LLDB, via `lldb-dap`.

Both run the stock adapter behind a thin **Debug Adapter Protocol proxy** that fixes the
coroutine gaps at the DAP layer. The proxy runs inline in the extension host (no extra process).

## How it works

A suspended coroutine's continuation chain and spilled locals aren't visible to a normal stack
walk. The two debuggers fall short in opposite ways, so each type applies a different fix:

**GDB (`cppdbg-tmc`)** — GDB frame filters *can* synthesize the async frames, but stock MIEngine
crashes on them (they have no `level`). So the proxy runs with frame filters **off** and
reconstructs everything itself:
- launches with `coro_backtrace_gdb.py` loaded but **without** `-enable-frame-filters`;
- on `stackTrace`, evaluates `$__tmc_async_chain()` and **splices the async frames in** (building
  a cached augmented stack — correct under paging and nested stops);
- for their `scopes`/`variables`, evaluates `$__coro_local_names()` +
  `(*$__coro_frame_at(addr)).<name>`.

**LLDB (`lldb-dap-tmc`)** — LLDB's `ScriptedFrameProvider` already shows the async frames
natively; the only gap is **locals** (LLDB 22.x doesn't yet invoke the `get_variables` hook —
LLVM PR #178575). So the proxy passes frames through untouched and only synthesizes locals:
- launches with `coro_backtrace_lldb.py` loaded (registers the frame provider + a
  `tmc-coro-locals` command that reuses the exact `get_variables` logic);
- notes which frames are `[async]`, and for their `scopes`/`variables` evaluates
  `tmc-coro-locals` and slices out that frame's locals.

Both stopgaps go away once the respective upstream fixes ship (MIEngine synthetic-frame support;
LLVM PR #178575); the extension can then drop the proxy for that debugger.

## Requirements

- **GDB path:** the **C/C++ extension** (`ms-vscode.cpptools`, reused for its `OpenDebugAD7`) and **GDB** with Python.
- **LLDB path:** **LLDB 22+** with `lldb-dap` (from the `llvm-vs-code-extensions.lldb-dap` extension or on `PATH`).
- A debuggee built with debug info (Clang or GCC).

## Use

Set your launch configuration `type` to **`cppdbg-tmc`** or **`lldb-dap-tmc`**. Everything else is
a normal `cppdbg` / `lldb-dap` launch — the extension injects the coroutine script (and, for GDB,
pretty-printing) for you:

```json
{
  "type": "cppdbg-tmc",
  "request": "launch",
  "name": "(gdb) Launch + TMC coroutines",
  "program": "${workspaceFolder}/build/clang-linux-debug/fib",
  "cwd": "${workspaceFolder}",
  "MIMode": "gdb"
}
```
```json
{
  "type": "lldb-dap-tmc",
  "request": "launch",
  "name": "(lldb) Launch + TMC coroutines",
  "program": "${workspaceFolder}/build/clang-linux-debug/fib",
  "cwd": "${workspaceFolder}"
}
```

Stop inside a coroutine and the Call Stack shows `[async]` continuation frames; selecting one
populates the Variables pane with that coroutine's locals.

## Install (development)

This is an unpacked extension. From VS Code: **Run and Debug → "Run Extension"**, or copy this
folder into `~/.vscode/extensions/` and reload. No `npm install` is needed (no external deps).

## Headless tests (no GUI)

`proxy_test.py` drives the proxy against the stock adapter and prints the reconstructed frames
and locals; `standalone.js` is the stdio wrapper it spawns. Requires Node and the `fib` sample:

```sh
python3 proxy_test.py
```

## Known limitations / notes

- Loading the GDB script sets `print characters unlimited` (full string display) so the proxy's
  base64 payloads aren't truncated at 200 chars. Array element counts are left bounded.
- Async frames are reconstructed on demand: the first `stackTrace` for a coroutine thread at a
  stop materializes the full augmented stack (real frames + async frames spliced in after the
  coroutine frame) and caches it; later pages are served as slices of that model. This is correct
  regardless of where the coroutine frame sits (nested calls) and how the client pages the stack.
  Non-coroutine threads are forwarded verbatim, so ordinary debugging incurs no extra work.
- Synthetic-frame locals that are themselves structs expand normally (their child references are
  the underlying adapter's, passed through).
- Verified with Clang on Linux/GDB. The GCC path reuses the same decorators but hasn't been
  exercised through the proxy yet.
- `$__coro_frame_at` / `$__coro_local_names` / `$__tmc_async_chain` live in
  `coro_backtrace_gdb.py`; `tmc-coro-locals` lives in `coro_backtrace_lldb.py` (both bundled).
  Keep these copies in sync with the canonical scripts.
- **LLDB locals are currently flat** (scalars show values; aggregates show `{...}` and aren't
  expandable), because they're transported from a command rather than as live values. The GDB
  path supports nested expansion. Full LLDB expansion arrives once LLVM PR #178575 ships and the
  LLDB proxy can be dropped.
