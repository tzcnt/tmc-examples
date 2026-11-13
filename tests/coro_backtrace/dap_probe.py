#!/usr/bin/env python3
"""
Drives lldb-dap the way VS Code does and records the call stack it reports.
Started by run.py:

    CBT_PARAMS='{...}' python3 dap_probe.py

Parameters (JSON in CBT_PARAMS): out, script, binary, lldb_dap, and tag: the
program is continued until stop_here(tag). The observation has the stack frames
of that stop (name, line, source file, presentationHint) and the result of
evaluating an expression in the first async frame, as a Watch and in the Debug
Console.
"""
import json
import os
import select
import subprocess
import sys
import time

P = json.loads(os.environ["CBT_PARAMS"])
OUT = {"stops": [], "errors": [], "extra": {}, "completed": False}
TIMEOUT = 60


class Dap:
    def __init__(self, executable):
        self.proc = subprocess.Popen(
            [executable], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0
        )
        self.seq = 0

    def send(self, command, arguments=None):
        self.seq += 1
        body = json.dumps({"seq": self.seq, "type": "request", "command": command, "arguments": arguments or {}}).encode()
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.proc.stdin.flush()
        return self.seq

    def _read(self, count, deadline):
        data = b""
        while len(data) < count:
            ready, _, _ = select.select([self.proc.stdout], [], [], max(0, deadline - time.time()))
            chunk = self.proc.stdout.read(count - len(data)) if ready else b""
            if not chunk:
                raise RuntimeError("lldb-dap stopped answering (exit code %s)" % self.proc.poll())
            data += chunk
        return data

    def receive(self, deadline):
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            header += self._read(1, deadline)
        return json.loads(self._read(int(header.split(b":")[1]), deadline))

    def wait_for(self, predicate):
        deadline = time.time() + TIMEOUT
        while True:
            message = self.receive(deadline)
            if predicate(message):
                return message

    def request(self, command, arguments=None):
        seq = self.send(command, arguments)
        return self.wait_for(lambda m: m.get("type") == "response" and m.get("request_seq") == seq)

    def wait_for_stop(self):
        stop = self.wait_for(lambda m: m.get("type") == "event" and m.get("event") in ("stopped", "exited", "terminated"))
        if stop["event"] != "stopped":
            raise RuntimeError("the program ended before stop_here(%d)" % P["tag"])
        return stop["body"]["threadId"]


def evaluate(dap, frame_id, expression, context):
    response = dap.request("evaluate", {"expression": expression, "frameId": frame_id, "context": context})
    return (response.get("body") or {}).get("result") if response.get("success") else None


def main():
    dap = Dap(P["lldb_dap"])
    try:
        dap.request("initialize", {"adapterID": "lldb-dap", "linesStartAt1": True, "columnsStartAt1": True, "pathFormat": "path"})
        dap.send("launch", {
            "program": P["binary"],
            "cwd": os.getcwd(),
            # As in .vscode/launch.json: the script must be imported after the target exists.
            "preRunCommands": ["command script import " + P["script"]],
        })
        dap.wait_for(lambda m: m.get("type") == "event" and m.get("event") == "initialized")
        dap.request("setFunctionBreakpoints", {"breakpoints": [{"name": "stop_here"}]})
        dap.send("configurationDone")
        thread = dap.wait_for_stop()
        for _ in range(200):
            frames = dap.request("stackTrace", {"threadId": thread, "startFrame": 0, "levels": 20})["body"]["stackFrames"]
            tag = evaluate(dap, frames[0]["id"], "tag", "watch") or evaluate(dap, frames[0]["id"], "(int)$rdi", "watch")
            if tag is not None and int(tag) == P["tag"]:
                break
            dap.send("continue", {"threadId": thread})
            thread = dap.wait_for_stop()
        else:
            raise RuntimeError("never stopped in stop_here(%d)" % P["tag"])

        # lldb-dap marks artificial frames with the `subtle` hint (greyed out in VS Code).
        async_frames = [f for f in frames if f.get("presentationHint") == "subtle"]
        stop = {"tag": P["tag"], "frames": []}
        for f in async_frames:
            name = f["name"]
            fork = name.startswith("[fork] ")
            stop["frames"].append({
                "name": name[len("[fork] "):] if fork else name,
                "fork": fork,
                "file": (f.get("source") or {}).get("name"),
                "line": f.get("line"),
            })
        if async_frames:
            frame_id = async_frames[0]["id"]
            results = [evaluate(dap, frame_id, "20+22", context) for context in ("watch", "repl")]
            stop["frames"][0]["expr"] = all(r is not None and "42" in r for r in results)
            OUT["extra"]["evaluate"] = results
        OUT["extra"]["all_frames"] = [[f["name"], f.get("line"), f.get("presentationHint")] for f in frames]
        OUT["stops"].append(stop)
        # A crashed adapter shows here: it would not answer.
        dap.request("threads")
        dap.send("disconnect", {"terminateDebuggee": True})
    finally:
        time.sleep(0.2)
        dap.proc.kill()


try:
    main()
    OUT["completed"] = True
except Exception as e:
    OUT["errors"].append("%s: %s" % (type(e).__name__, e))
with open(P["out"], "w") as f:
    json.dump(OUT, f)
sys.exit(0 if OUT["completed"] else 1)
