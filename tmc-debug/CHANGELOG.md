# Changelog

## 0.0.1

- Initial release.
- `cppdbg-tmc` debug type: runs the stock cpptools `OpenDebugAD7` behind a DAP proxy that
  reconstructs TooManyCooks coroutine async call frames and their locals — no patched debug
  adapter and no `-enable-frame-filters` required.
- Auto-injects the bundled GDB coroutine script and pretty-printing into `cppdbg-tmc` launches.
- Correct under stack paging and nested (non-coroutine top frame) stops.
