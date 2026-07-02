#!/usr/bin/env python3
"""Probe lldb-dap: confirm async frames show natively, and learn how `evaluate`
returns output for commands / script / expressions (to pick the locals transport)."""
import json, os, re, subprocess, threading, time

WS = "/home/tzcnt/github/tmc-examples"
PROGRAM = WS + "/build/clang-linux-debug/fib"
SCRIPT = WS + "/coro_backtrace_lldb.py"
SRC = WS + "/examples/fib.cpp"
BP_LINE = 44
LLDB_DAP = "/usr/bin/lldb-dap"

proc = subprocess.Popen([LLDB_DAP], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None, bufsize=0)
_seq = 0
def request(command, arguments=None):
    global _seq
    _seq += 1
    msg = {"seq": _seq, "type": "request", "command": command, "arguments": arguments or {}}
    data = json.dumps(msg).encode()
    proc.stdin.write(f"Content-Length: {len(data)}\r\n\r\n".encode() + data); proc.stdin.flush()
    return _seq

_init = threading.Event(); _done = threading.Event()
_evals = {}

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
            for f in frames[:8]:
                nm = f.get("name") or ""
                print(f"  #{f['id']} {nm[:70]}{' <==async' if '[async]' in nm else ''}")
            # transport probes
            _evals[request("evaluate", {"expression": "version", "context": "repl"})] = "cmd:version"
            _evals[request("evaluate", {"expression": "script 40+2", "context": "repl"})] = "script"
            _evals[request("evaluate", {"expression": "p 40+2", "context": "repl"})] = "expr:p"
        elif cmd == "evaluate":
            label = _evals.get(msg.get("request_seq"), "?")
            body = msg.get("body", {})
            print(f"EVAL[{label}] success={ok} result={repr((body.get('result') or '')[:80])}")
            if len(_evals) and all(s in [m for m in _evals] for s in _evals):
                pass
            _evals_done.add(msg.get("request_seq"))
            if len(_evals_done) >= 3: _done.set()

_evals_done = set()

def on_stopped(tid):
    time.sleep(0.2)
    request("stackTrace", {"threadId": tid, "startFrame": 0, "levels": 20})

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
request("initialize", {"clientID": "t", "adapterID": "lldb-dap", "linesStartAt1": True, "columnsStartAt1": True, "pathFormat": "path"})
time.sleep(0.3)
request("launch", {"type": "lldb-dap", "request": "launch", "name": "t", "program": PROGRAM, "args": [],
                   "cwd": WS, "stopOnEntry": False,
                   "preRunCommands": [f"command script import {SCRIPT}"]})
if not _init.wait(timeout=25): print("!! no initialized"); proc.kill(); exit(1)
request("setBreakpoints", {"source": {"path": SRC, "name": "fib.cpp"}, "breakpoints": [{"line": BP_LINE}]})
time.sleep(0.4)
request("configurationDone", {})
if not _done.wait(timeout=45): print("!! timeout")
time.sleep(0.3)
request("disconnect", {"terminateDebuggee": True}); time.sleep(0.3); proc.kill()
