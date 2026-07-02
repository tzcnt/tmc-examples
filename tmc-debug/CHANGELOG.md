# Changelog

## 0.1.0

- Add **`lldb-dap-tmc`** debug type: async coroutine frames already show natively via LLDB's
  ScriptedFrameProvider; the proxy fills in their locals (which LLDB 22.x doesn't yet populate —
  LLVM PR #178575) by evaluating a `tmc-coro-locals` command that reuses `get_variables`.
- Mark LLDB async frames with an `[async]` prefix, consistent with the GDB path.
- Refactor shared DAP-proxy machinery into `dapProxyBase.js`.

## 0.0.1

- Initial release.
- `cppdbg-tmc` debug type: runs the stock cpptools `OpenDebugAD7` behind a DAP proxy that
  reconstructs TooManyCooks coroutine async call frames and their locals — no patched debug
  adapter and no `-enable-frame-filters` required.
- Auto-injects the bundled GDB coroutine script and pretty-printing into `cppdbg-tmc` launches.
- Correct under stack paging and nested (non-coroutine top frame) stops.
