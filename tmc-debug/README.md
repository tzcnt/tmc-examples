# TooManyCooks Coroutine Debugging (VS Code)

Shows the **async coroutine call stack** and each suspended coroutine's **locals** in VS Code,
using the **stock** Microsoft C/C++ debugger (`cppdbg` / `OpenDebugAD7`) — **no patched debug
adapter, no MIEngine fork**.

## How it works

A suspended coroutine's continuation chain and its spilled locals are not visible to a normal
stack walk. GDB frame filters can synthesize those frames, but stock MIEngine crashes on them
(the synthetic frames have no `level`). Instead of patching MIEngine, this extension runs the
stock `OpenDebugAD7` behind a thin **Debug Adapter Protocol proxy** (`tmcProxy.js`):

- It launches the debuggee with the GDB coroutine script (`coro_backtrace_gdb.py`) loaded but
  **without** `-enable-frame-filters` — so stock MIEngine sees a normal stack and never crashes.
- On each `stackTrace`, it evaluates `$__tmc_async_chain()` and **splices the async frames in**.
- For those frames' `scopes`/`variables`, it evaluates `$__coro_local_names()` and
  `(*$__coro_frame_at(addr)).<name>` to produce the locals.
- Everything else passes straight through — ordinary debugging is untouched and native-speed.

The proxy runs inline in the extension host (no extra process, no runtime dependency beyond the
C/C++ extension it reuses).

## Requirements

- The **C/C++ extension** (`ms-vscode.cpptools`) — reused for its bundled `OpenDebugAD7`.
- **GDB** (with Python), and a debuggee built with debug info by Clang or GCC.

## Use

Set your launch configuration `type` to **`cppdbg-tmc`**. Everything else is a normal `cppdbg`
launch — the extension injects the GDB script and pretty-printing for you:

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
  `coro_backtrace_gdb.py` (bundled). Keep this copy in sync with the canonical script.
