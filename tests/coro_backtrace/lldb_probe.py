"""
Runs inside LLDB and records what coro_backtrace_lldb.py shows. Started by run.py:

    CBT_PARAMS='{...}' lldb --batch --no-lldbinit \
        -o 'command script import lldb_probe.py' -o cbt_probe

It only observes. run.py decides what is right. The parameters are those of
gdb_probe.py (out, script, binary, break, ignore, max_stops, only_tags,
all_threads, intel, cycle, save_core, core), plus:

    early_import  import the script once before the target exists
    expr          evaluate an expression on (at most this many) async frames per thread

The frame mode comes from the environment (CORO_BT_FRAME_MODE), as for a user.
"""
import json
import os
import time
import traceback

import lldb

P = json.loads(os.environ["CBT_PARAMS"])
OUT = {"stops": [], "errors": [], "extra": {}, "completed": False}


def command(debugger, text):
    result = lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand(text, result)
    return (result.GetOutput() or "") + (result.GetError() or "")


def is_async(frame):
    # LLDB also calls its own tail-call frames artificial, so prefer IsSynthetic.
    return frame.IsSynthetic() if hasattr(frame, "IsSynthetic") else frame.IsArtificial()


def evaluate(debugger, process, thread, index):
    """Evaluate an expression with the async frame selected, and ask what an IDE asks."""
    process.SetSelectedThread(thread)
    thread.SetSelectedFrame(index)
    frame = thread.GetFrameAtIndex(index)
    value = frame.EvaluateExpression("20+22")
    text = command(debugger, "expression -- 20+22")
    frame.GetDisplayFunctionName()
    frame.GetVariables(True, True, False, True).GetSize()
    frame.FindVariable("this").IsValid()
    frame.GetValueForVariablePath("this")
    frame.GetRegisters().GetSize()
    frame.GetCFA()
    frame.GetSymbolContext(lldb.eSymbolContextEverything)
    return value.GetValueAsSigned(0) == 42 and "42" in text


def async_frames(debugger, process, thread):
    frames = []
    for index in range(1000):
        frame = thread.GetFrameAtIndex(index)
        if not frame.IsValid():
            break
        if not is_async(frame):
            continue
        name = frame.GetFunctionName() or ""
        fork = name.startswith("[fork] ")
        entry = frame.GetLineEntry()
        frames.append({
            "name": name[len("[fork] "):] if fork else name,
            "fork": fork,
            "file": entry.GetFileSpec().GetFilename(),
            "line": entry.GetLine(),
            "index": index,
        })
    for f in frames[:P.get("expr", 0)]:
        f["expr"] = evaluate(debugger, process, thread, f["index"])
    return frames


def read_tag(thread):
    frame = thread.GetFrameAtIndex(0)
    tag = frame.FindVariable("tag")
    if tag.IsValid() and tag.GetError().Success():
        return tag.GetValueAsSigned(-1)
    for reg in ("rdi", "x0"):  # first argument register
        value = frame.FindRegister(reg)
        if value.IsValid():
            return value.GetValueAsUnsigned(0) & 0xFFFFFFFF
    return -1


def make_cycle(process, thread):
    """Make the coroutine that awaits the running one its own continuation."""
    frame = thread.GetFrameAtIndex(1)  # the coroutine that called stop_here()
    var = frame.FindVariable("__coro_frame")  # Clang
    if var.IsValid():
        base = var.GetLoadAddress()
        promise = var.GetChildMemberWithName("__promise")
    else:
        var = frame.FindVariable("frame_ptr")  # GCC
        base = var.GetValueAsUnsigned(0)
        promise = var.Dereference().GetChildMemberWithName("_Coro_promise")
    cont = promise.GetChildMemberWithName("customizer").GetChildMemberWithName("continuation")
    if not cont.IsValid() or not base:
        raise RuntimeError("could not find the coroutine frame of the running coroutine")
    parent = cont.GetValueAsUnsigned(0)
    # The parent has the same promise type here, so its slot is at the same offset.
    error = lldb.SBError()
    size = process.GetAddressByteSize()
    process.WriteMemory(parent + cont.GetLoadAddress() - base, parent.to_bytes(size, "little"), error)
    if not error.Success():
        raise RuntimeError("memory write failed: %s" % error.GetCString())
    # The frames of this stop were built before the write. A step gives a new stop.
    thread.StepInstruction(False)


def record_stop(debugger, process, thread):
    stop = {"tag": read_tag(thread) if P.get("tags", True) else None}
    if P.get("cycle"):
        make_cycle(process, thread)
    stop["frames"] = async_frames(debugger, process, thread)
    if P.get("all_threads"):
        stop["others"] = [
            async_frames(debugger, process, other)
            for other in process
            if other.GetThreadID() != thread.GetThreadID()
        ]
    OUT["stops"].append(stop)


def import_script(debugger):
    return command(debugger, "command script import " + P["script"])


def main(debugger):
    debugger.SetAsync(False)
    if P.get("intel"):
        command(debugger, "settings set target.x86-disassembly-flavor intel")
    if P.get("early_import"):
        import_script(debugger)  # prints why it did not register; run.py looks for that
    target = debugger.CreateTarget(P["binary"])
    if not target.IsValid():
        raise RuntimeError("cannot create a target for " + P["binary"])

    if P.get("core"):
        process = target.LoadCore(P["core"])
        import_script(debugger)
        # LLDB's event thread now builds the frames of the core's threads, which constructs
        # the frame provider on that thread. Asking for frames from here at the same time
        # deadlocks inside LLDB (LLDB 22: this thread holds the API lock and waits for the
        # frame list, the event thread holds the frame list and waits for the API lock).
        time.sleep(2)
        for thread in process:
            if "stop_here" in (thread.GetFrameAtIndex(0).GetFunctionName() or ""):
                record_stop(debugger, process, thread)
                break
        else:
            OUT["errors"].append("no thread of the core file is in stop_here()")
        return

    import_script(debugger)
    where = P["break"]
    if ":" in where:
        filename, line = where.rsplit(":", 1)
        breakpoint = target.BreakpointCreateByLocation(filename, int(line))
    else:
        breakpoint = target.BreakpointCreateByName(where)
    if P.get("ignore"):
        breakpoint.SetIgnoreCount(P["ignore"])
    only_tags = P.get("only_tags")
    process = target.LaunchSimple(None, None, os.getcwd())
    while process.GetState() == lldb.eStateStopped:
        thread = next((t for t in process if t.GetStopReason() == lldb.eStopReasonBreakpoint), None)
        if thread is None:
            OUT["errors"].append("unexpected stop: " + str(process.GetSelectedThread()))
            process.Kill()
            return
        if only_tags is None or read_tag(thread) in only_tags:
            record_stop(debugger, process, thread)
            if P.get("save_core"):
                OUT["extra"]["save_core"] = command(debugger, "process save-core " + P["save_core"])
            if P.get("cycle") or P.get("save_core") or len(OUT["stops"]) >= P.get("max_stops", 10**9):
                process.Kill()
                return
        process.Continue()
    if process.GetState() != lldb.eStateExited:
        OUT["errors"].append("process ended in state %d" % process.GetState())


def cbt_probe(debugger, args, result, internal_dict):
    try:
        main(debugger)
        OUT["completed"] = True
    except Exception:
        OUT["errors"].append(traceback.format_exc())
    with open(P["out"], "w") as f:
        json.dump(OUT, f)


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f lldb_probe.cbt_probe cbt_probe")
