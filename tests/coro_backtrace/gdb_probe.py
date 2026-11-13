"""
Runs inside GDB and records what coro_backtrace_gdb.py shows. Started by run.py:

    CBT_PARAMS='{...}' gdb --batch -nx -x gdb_probe.py

It only observes. run.py decides what is right. Parameters (JSON in CBT_PARAMS):

    out         where to write the observations (JSON)
    script      path of coro_backtrace_gdb.py
    binary      the test program
    break       breakpoint location (function, or file:line)
    ignore      ignore count for the breakpoint
    max_stops   stop after this many recorded stops
    only_tags   record only stops with these tags (others are continued)
    all_threads also record the async frames of the other threads
    twice       source the script twice
    intel       set disassembly-flavor intel
    print_at    {tag: [expressions]} to print at the stop with that tag
    cycle       make the continuation chain cyclic before recording (debug builds)
    save_core   write a core file at the first stop
    core        load this core file instead of running the program
"""
import json
import os
import re
import traceback

import gdb
import gdb.frames

P = json.loads(os.environ["CBT_PARAMS"])
OUT = {"stops": [], "errors": [], "extra": {}, "completed": False}

_FIRST_ARG_REGISTER = {"i386:x86-64": "rdi", "aarch64": "x0"}
_stop_events = []


def async_frames():
    """The [async] frames of the selected thread, as the frame filter gives them to `bt`."""
    frames = []
    for deco in gdb.frames.execute_frame_filters(gdb.newest_frame(), 0, -1) or []:
        if type(deco).__name__ != "CoroutineFrameDecorator":
            continue
        name = deco.function() or ""
        fork = name.startswith("[fork] ")
        if fork:
            name = name[len("[fork] "):]
        if name.startswith("[async] "):
            name = name[len("[async] "):]
        filename = deco.filename()
        frames.append({
            "name": name,
            "fork": fork,
            "file": os.path.basename(filename) if filename else None,
            "line": deco.line(),
        })
    return frames


def read_tag():
    frame = gdb.newest_frame()
    try:
        return int(frame.read_var("tag"))
    except Exception:
        reg = _FIRST_ARG_REGISTER[frame.architecture().name()]
        return int(frame.read_register(reg)) & 0xFFFFFFFF


def resume(command):
    """Run or continue. Returns 'breakpoint', 'exited', or a description of another stop."""
    del _stop_events[:]
    try:
        gdb.execute(command, to_string=True)
    except gdb.error as e:
        return "error: %s" % e
    if not _stop_events:
        return "exited"
    event = _stop_events[-1]
    if isinstance(event, gdb.BreakpointEvent):
        return "breakpoint"
    return "stopped: %s" % type(event).__name__


def make_cycle():
    """Make the coroutine that awaits the running one its own continuation."""
    gdb.newest_frame().older().select()  # the coroutine that called stop_here()
    for base_expr, slot_expr in (
        ("(unsigned long)&__coro_frame", "&__coro_frame.__promise.customizer.continuation"),  # Clang
        ("(unsigned long)frame_ptr", "&frame_ptr->_Coro_promise.customizer.continuation"),  # GCC
    ):
        try:
            base = int(gdb.parse_and_eval(base_expr))
            slot = gdb.parse_and_eval(slot_expr)
        except gdb.error:
            continue
        parent = int(slot.dereference())
        # The parent has the same promise type here, so its slot is at the same offset.
        gdb.execute("set {unsigned long}%d = %d" % (parent + int(slot) - base, parent))
        gdb.newest_frame().select()
        return
    raise RuntimeError("could not find the coroutine frame of the running coroutine")


def unguard_stacks():
    """
    glibc 2.42+ puts the guard page of a thread stack inside the stack's mapping
    (MADV_GUARD_INSTALL), and gcore (GDB 17.2) abandons a mapping at the first
    page it cannot read, so the core would have no thread stacks. Remove the
    guards: the process is killed right after the dump.
    """
    inferior = gdb.selected_inferior()
    with open("/proc/%d/maps" % inferior.pid) as f:
        maps = [(int(a, 16), int(b, 16)) for a, b in re.findall(r"^([0-9a-f]+)-([0-9a-f]+) rw", f.read(), re.M)]
    current = gdb.selected_thread()
    stack_pointers = []
    for thread in inferior.threads():
        thread.switch()
        stack_pointers.append(int(gdb.parse_and_eval("$sp")))
    current.switch()
    for sp in stack_pointers:
        for low, high in maps:
            if low <= sp < high:
                madv_guard_remove = 103
                try:
                    gdb.parse_and_eval("((int(*)(void*, unsigned long, int))madvise)((void*)%d, %d, %d)" % (low, high - low, madv_guard_remove))
                except gdb.error:
                    pass


def record_stop():
    stop = {"tag": read_tag() if P.get("tags", True) else None}
    if P.get("cycle"):
        make_cycle()
    stop["frames"] = async_frames()
    if P.get("all_threads"):
        current = gdb.selected_thread()
        stop["others"] = []
        for thread in gdb.selected_inferior().threads():
            if thread.num != current.num:
                thread.switch()
                stop["others"].append(async_frames())
        current.switch()
    for expr in P.get("print_at", {}).get(str(stop["tag"]), []):
        try:
            text = gdb.execute("print " + expr, to_string=True)
        except gdb.error as e:
            text = "ERROR: %s" % e
        OUT["extra"].setdefault("printed", {})[expr] = text
    OUT["stops"].append(stop)


def main():
    for setting in ("pagination off", "confirm off", "width 0", "debuginfod enabled off"):
        gdb.execute("set " + setting)
    if P.get("intel"):
        gdb.execute("set disassembly-flavor intel")
    gdb.execute("file " + P["binary"])
    for _ in range(2 if P.get("twice") else 1):
        gdb.execute("source " + P["script"])

    if P.get("core"):
        gdb.execute("core-file " + P["core"], to_string=True)
        for thread in gdb.selected_inferior().threads():
            thread.switch()
            if "stop_here" in (gdb.newest_frame().name() or ""):
                record_stop()
                break
        else:
            OUT["errors"].append("no thread of the core file is in stop_here()")
        return

    gdb.events.stop.connect(_stop_events.append)
    gdb.execute("break " + P["break"], to_string=True)
    if P.get("ignore"):
        gdb.execute("ignore 1 %d" % P["ignore"], to_string=True)
    only_tags = P.get("only_tags")
    state = resume("run")
    while state == "breakpoint":
        if only_tags is None or read_tag() in only_tags:
            record_stop()
            if P.get("save_core"):
                unguard_stacks()
                gdb.execute("gcore " + P["save_core"], to_string=True)
            if P.get("cycle") or P.get("save_core") or len(OUT["stops"]) >= P.get("max_stops", 10**9):
                gdb.execute("kill", to_string=True)
                return
        state = resume("continue")
    if state != "exited":
        OUT["errors"].append("unexpected stop: " + state)
        gdb.execute("kill", to_string=True)


try:
    main()
    OUT["completed"] = True
except Exception:
    OUT["errors"].append(traceback.format_exc())
with open(P["out"], "w") as f:
    json.dump(OUT, f)
