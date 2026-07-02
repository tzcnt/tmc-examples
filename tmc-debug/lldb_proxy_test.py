#!/usr/bin/env python3
"""Headless DAP driver for the LLDB proxy against stock lldb-dap.

Verifies: async frames show (native ScriptedFrameProvider) AND their locals are populated by
the proxy (working around LLDB 22.x not invoking get_variables yet)."""
import json, os, re, subprocess, threading, time

HOME = os.path.expanduser("~")
NODE = f"{HOME}/.node/bin/node"
HERE = os.path.dirname(os.path.abspath(__file__))
WS = "/home/tzcnt/github/tmc-examples"
PROGRAM = WS + "/build/clang-linux-debug/fib"
SCRIPT = WS + "/coro_backtrace_lldb.py"
SRC = WS + "/examples/fib.cpp"
BP_LINE = 44
LLDB_DAP = "/usr/bin/lldb-dap"

env = dict(os.environ, LLDB_DAP=LLDB_DAP)
proc = subprocess.Popen([NODE, "standalone.js"], cwd=HERE, env=env,
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None, bufsize=0)
_seq = 0
def request(command, arguments=None):
    global _seq
    _seq += 1
    msg = {"seq": _seq, "type": "request", "command": command, "arguments": arguments or {}}
    data = json.dumps(msg).encode()
    proc.stdin.write(f"Content-Length: {len(data)}\r\n\r\n".encode() + data); proc.stdin.flush()
    return _seq

_init = threading.Event(); _done = threading.Event()
_probe = {"expected": 0, "seen": 0}

def handle(msg):
    t = msg.get("type")
    if t == "event":
        ev = msg.get("event")
        if ev == "initialized": _init.set()
        elif ev == "stopped": threading.Thread(target=on_stopped, args=(msg["body"].get("threadId"),)).start()
        elif ev in ("terminated", "exited"): _done.set()
    elif t == "response":
        cmd, ok = msg.get("command"), msg.get("success")
        if cmd == "stackTrace" and ok:
            frames = msg["body"].get("stackFrames", [])
            print(f"stackTrace -> {len(frames)} frames")
            asyncs = [f for f in frames if "[async]" in (f.get("name") or "")]
            for f in frames[:8]:
                nm = f.get("name") or ""
                print(f"  #{f['id']} {nm[:66]}{'  <==async' if '[async]' in nm else ''}")
            _probe["expected"] = len(asyncs)
            for f in asyncs:
                request("scopes", {"frameId": f["id"]})
        elif cmd == "scopes" and ok:
            for s in msg["body"].get("scopes", []):
                if s.get("name") == "Locals":
                    request("variables", {"variablesReference": s["variablesReference"]})
        elif cmd == "variables":
            vs = msg.get("body", {}).get("variables", []) if ok else []
            print("  Locals -> " + (", ".join(f"{v.get('name')}={v.get('value')}" for v in vs) or "(empty)"))
            _probe["seen"] += 1
            if _probe["seen"] >= _probe["expected"] and _probe["expected"] > 0:
                _done.set()

def on_stopped(tid):
    time.sleep(0.2)
    request("stackTrace", {"threadId": tid, "startFrame": 0, "levels": 30})

def reader():
    buf = b""
    while True:
        c = proc.stdout.read(1)
        if not c: break
        buf += c
        if buf.endswith(b"\r\n\r\n"):
            m = re.search(rb"Content-Length:\s*(\d+)", buf); n = int(m.group(1)); body = b""
            while len(body) < n: body += proc.stdout.read(n - len(body))
            try: handle(json.loads(body))
            except Exception: pass
            buf = b""

threading.Thread(target=reader, daemon=True).start()
request("initialize", {"clientID": "t", "adapterID": "lldb-dap-tmc", "linesStartAt1": True, "columnsStartAt1": True, "pathFormat": "path"})
time.sleep(0.3)
request("launch", {"type": "lldb-dap-tmc", "request": "launch", "name": "t", "program": PROGRAM, "args": [],
                   "cwd": WS, "stopOnEntry": False, "preRunCommands": [f"command script import {SCRIPT}"]})
if not _init.wait(timeout=25): print("!! no initialized"); proc.kill(); exit(1)
request("setBreakpoints", {"source": {"path": SRC, "name": "fib.cpp"}, "breakpoints": [{"line": BP_LINE}]})
time.sleep(0.4)
request("configurationDone", {})
if not _done.wait(timeout=45): print("!! timeout")
time.sleep(0.3)
request("disconnect", {"terminateDebuggee": True}); time.sleep(0.3); proc.kill()
