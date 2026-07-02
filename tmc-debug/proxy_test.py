#!/usr/bin/env python3
"""Headless DAP driver for the TmcProxy against the STOCK cpptools OpenDebugAD7.

Verifies, with no MIEngine patch and frame filters OFF:
  1. the coroutine async frames are reconstructed in the stackTrace,
  2. their locals are served via scopes/variables, and
  3. paged stackTrace requests slice the augmented stack consistently (no gaps/dupes).
"""
import json, os, re, subprocess, sys, threading, time

HOME = os.path.expanduser("~")
NODE = f"{HOME}/.node/bin/node"
HERE = os.path.dirname(os.path.abspath(__file__))
WS = "/home/tzcnt/github/tmc-examples"
PROGRAM = WS + "/build/clang-linux-debug/fib"
GDB_SCRIPT = WS + "/coro_backtrace_gdb.py"
SRC = WS + "/examples/fib.cpp"
BP_LINE = 44
AD7_REAL = HOME + "/.vscode/extensions/ms-vscode.cpptools-1.32.2-linux-x64/debugAdapters/bin/OpenDebugAD7"

env = dict(os.environ, AD7_REAL=AD7_REAL)
proc = subprocess.Popen([NODE, "standalone.js"], cwd=HERE, env=env,
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None, bufsize=0)

_seq = 0
_lock = threading.Lock()
def request(command, arguments=None):
    global _seq
    with _lock:
        _seq += 1
        seq = _seq
    msg = {"seq": seq, "type": "request", "command": command, "arguments": arguments or {}}
    data = json.dumps(msg).encode()
    proc.stdin.write(f"Content-Length: {len(data)}\r\n\r\n".encode() + data)
    proc.stdin.flush()
    return seq

_done = threading.Event()
_initialized = threading.Event()
_tid = [None]
_full = {"seq": None, "frames": None}
_pages = {}            # seq -> label
_page_frames = {}      # label -> frames
_locals = {"expected": 0, "seen": 0}
_state = {"locals_done": False, "paging_done": False}

def maybe_finish():
    if _state["locals_done"] and _state["paging_done"]:
        _done.set()

def check_paging():
    a, b = _page_frames.get("A"), _page_frames.get("B")
    if a is None or b is None:
        return
    full_ids = [f["id"] for f in _full["frames"]]
    concat_ids = [f["id"] for f in a] + [f["id"] for f in b]
    ok = concat_ids == full_ids
    print(f"PAGING: pageA={len(a)} pageB={len(b)} concat==full? {ok}")
    if not ok:
        print("  full :", full_ids)
        print("  concat:", concat_ids)
    _state["paging_done"] = True
    maybe_finish()

def handle(msg):
    t = msg.get("type")
    if t == "event":
        ev, body = msg.get("event"), msg.get("body", {})
        if ev == "initialized":
            _initialized.set()
        elif ev == "stopped":
            _tid[0] = body.get("threadId")
            threading.Thread(target=on_stopped, args=(body.get("threadId"),)).start()
        elif ev in ("terminated", "exited"):
            _done.set()
        return
    if t != "response":
        return
    cmd, ok, rseq = msg.get("command"), msg.get("success"), msg.get("request_seq")
    if cmd == "stackTrace" and ok:
        frames = msg.get("body", {}).get("stackFrames", [])
        if rseq == _full["seq"]:
            _full["frames"] = frames
            print(f"stackTrace(full) -> {len(frames)} frames, totalFrames={msg['body'].get('totalFrames')}")
            for f in frames:
                tag = " <== async" if "[async]" in (f.get("name") or "") else ""
                print(f"  #{f['id']} {f.get('name')}  {(f.get('source') or {}).get('name')}:{f.get('line')}{tag}")
            # (2) locals probe on async frames + one real frame
            targets = [f for f in frames if "[async]" in (f.get("name") or "")] + \
                      [f for f in frames if "[async]" not in (f.get("name") or "")][:1]
            _locals["expected"] = len(targets)
            for f in targets:
                request("scopes", {"frameId": f["id"]})
            # (3) paging: two adjacent slices should concatenate to the full stack
            _pages[request("stackTrace", {"threadId": _tid[0], "startFrame": 0, "levels": 3})] = "A"
            _pages[request("stackTrace", {"threadId": _tid[0], "startFrame": 3, "levels": 200})] = "B"
        elif rseq in _pages:
            _page_frames[_pages[rseq]] = frames
            check_paging()
    elif cmd == "scopes" and ok:
        for s in msg.get("body", {}).get("scopes", []):
            if s.get("name") == "Locals":
                request("variables", {"variablesReference": s["variablesReference"]})
    elif cmd == "variables":
        vs = msg.get("body", {}).get("variables", []) if ok else []
        print("  Locals -> " + ", ".join(f"{v.get('name')}={v.get('value')}" for v in vs[:8]))
        _locals["seen"] += 1
        if _locals["seen"] >= _locals["expected"]:
            _state["locals_done"] = True
            maybe_finish()

def on_stopped(tid):
    time.sleep(0.2)
    _full["seq"] = request("stackTrace", {"threadId": tid, "startFrame": 0, "levels": 0})

def reader():
    buf = b""
    while True:
        c = proc.stdout.read(1)
        if not c: break
        buf += c
        if buf.endswith(b"\r\n\r\n"):
            m = re.search(rb"Content-Length:\s*(\d+)", buf)
            n = int(m.group(1)); body = b""
            while len(body) < n: body += proc.stdout.read(n - len(body))
            try: handle(json.loads(body))
            except Exception: pass
            buf = b""

threading.Thread(target=reader, daemon=True).start()

request("initialize", {"clientID": "t", "adapterID": "cppdbg-tmc", "linesStartAt1": True, "columnsStartAt1": True, "pathFormat": "path"})
time.sleep(0.3)
request("launch", {
    "type": "cppdbg-tmc", "request": "launch", "name": "t", "program": PROGRAM, "args": [],
    "stopAtEntry": False, "cwd": WS, "MIMode": "gdb",
    "miDebuggerArgs": f"-x {GDB_SCRIPT}",
    "setupCommands": [{"text": "-enable-pretty-printing", "ignoreFailures": True}],
})
if not _initialized.wait(timeout=25):
    print("!! no initialized"); proc.kill(); sys.exit(1)
request("setBreakpoints", {"source": {"path": SRC, "name": "fib.cpp"}, "breakpoints": [{"line": BP_LINE}]})
time.sleep(0.4)
request("configurationDone", {})
if not _done.wait(timeout=45):
    print("!! timeout")
time.sleep(0.3)
request("disconnect", {"terminateDebuggee": True})
time.sleep(0.3)
proc.kill()
