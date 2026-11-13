#!/usr/bin/env python3
"""
Regression suite for coro_backtrace_gdb.py and coro_backtrace_lldb.py.

Builds the programs in programs/ with every compiler it finds, in the flavors
below, runs them under GDB and LLDB with the scripts loaded, and compares the async
frames with the co_await sites marked in the sources. See README.md.

    tests/coro_backtrace/run.py                      # everything
    tests/coro_backtrace/run.py --compilers clang++,g++ --debuggers gdb
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from collections import Counter, namedtuple
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PROGRAMS = ("awaits", "member", "threads")

Compiler = namedtuple("Compiler", "exe family major version")
Build = namedtuple("Build", "compiler flavor label dir")
Job = namedtuple("Job", "build debugger mode test")

# Build flavors: name -> (flags, compiler family it is for, or None for all).
#
# "O2" is what most users build. GCC's default (generic) tuning splits the store
# of the suspend index into two instructions there. "O2-zen" keeps it as one
# instruction, which is what -march=native gives on an AMD CPU (this repo's
# presets use -march=native). The scripts have to read both forms.
FLAVORS = {
    "O0": (["-O0"], None),
    "O2": (["-O2", "-DNDEBUG"], None),
    "O2-zen": (["-O2", "-DNDEBUG", "-mtune=znver3"], "gcc"),
}

# ---------------------------------------------------------------------------
# What each stop must show
#
# A Site is one async frame: a substring of the function name, the anchor of
# its co_await in the program source, whether the await goes through a group
# (`[fork]`), and the exact name coro_backtrace_lldb.py gives it with GCC.
# ---------------------------------------------------------------------------

Site = namedtuple("Site", "function anchor fork gcc_name")
Site.__new__.__defaults__ = (False, None)
CLIENT_MAIN = Site("client_main_awaiter", "client_main", False, "tmc::detail::client_main_awaiter")


def awaits_chain(tag):
    if tag == 30:
        # `foreign` is not a TMC task: it is shown, and its chain ends there.
        return [Site("foreign", "site30", False, "foreign")]
    group = "shapes" if tag < 20 else "groups" if tag < 30 else "big" if tag == 40 else "braided"
    return [
        Site(group, "site%d" % tag, tag in (20, 21, 22, 23), group),
        Site("top", "top_" + group, False, "top"),
        Site("operator()", "main", False, "main::'lambda'()::operator()"),
        CLIENT_MAIN,
    ]


# tag -> how often the program stops there
AWAITS_STOPS = {1: 2, 2: 2, 3: 4, 4: 1, 5: 1, 6: 2, 7: 2, 8: 2, 9: 2, 20: 1, 21: 1, 22: 2, 23: 1, 24: 1, 30: 1, 40: 1, 50: 1}
AWAITS = {tag: awaits_chain(tag) for tag in AWAITS_STOPS}
# `foreign` runs inside the main lambda's resume. GDB starts a chain at every
# coroutine on the physical stack, so it also shows who awaits that lambda.
AWAITS_GDB = {**AWAITS, 30: AWAITS[30] + [CLIENT_MAIN]}

MEMBER_STOPS = {1: 1, 2: 1}
MEMBER = {
    1: [
        Site("Widget::method", "site1", False, "Widget::method"),
        Site("operator()", "main1", False, "main::'lambda'()::operator()"),
        CLIENT_MAIN,
    ],
    2: [
        Site("operator()", "site2", False, "Widget::via_lambda()::'lambda'(int)::operator()"),
        Site("Widget::via_lambda", "site3", False, "Widget::via_lambda"),
        Site("operator()", "main2", False, "main::'lambda'()::operator()"),
        CLIENT_MAIN,
    ],
}

# threads: any number of `fib` frames, then (if the chain gets that far) this tail.
THREADS_FIB = [Site("fib", "fib_plain", False, "fib"), Site("fib", "fib_fork", True, "fib")]
THREADS_TAIL = [
    Site("top_fib", "top", False, "top_fib"),
    Site("operator()", "main", False, "main::'lambda'(unsigned long)::operator()"),
    CLIENT_MAIN,
]

PRINTED = {
    "g_null_handle": ["coro(nullptr)"],
    "g_garbage_handle": ["coro(0x"],
    "g_live_handle": ["coro(0x", "foreign"],
    "*$get_coro_promise(g_live_handle)": ["junk0"],
}

# ---------------------------------------------------------------------------
# Known limitations
#
# Results that are wrong and expected to be: limitations of a compiler's debug
# info or of a debugger, as described in the Known Limitations of CLAUDE.md.
# A field that is left out matches anything; a tuple lists alternatives, and
# `major` may be a function. The key "*" in `items` stands for every stop of
# the test. Later entries win.
# A test whose failures are exactly the listed ones is reported as XFAIL. If a
# listed failure does not happen, the test is reported as XPASS: update the
# entry. Recorded with GDB 17.2 and LLDB 22.1.8.
# ---------------------------------------------------------------------------

GCC_OPT = dict(family="gcc", flavor=("O2", "O2-zen"))

KNOWN = [
    dict(GCC_OPT, debugger="gdb", test="awaits", items={"30": "chain"},
         reason="GCC -O2: `frame_ptr` of the main lambda is optimized out where it calls `foreign`, so GDB "
                "cannot show who awaits that lambda. The frame of `foreign` itself is right."),
    dict(GCC_OPT, debugger="lldb", test=("member", "edge", "core"), items={"1": "no-async", "2": "no-async"},
         reason="GCC -O2: `frame_ptr` stays in a caller-saved register across the call to stop_here(), "
                "and LLDB does not read those in a caller's frame (member.cpp does not use noipa)."),
    # dict mode: LLDB takes function and line from one pc, and with GCC -O2 some co_await
    # lines have no instruction that is not inside an inlined call. Which ones depends on
    # the compiler version and the tuning.
    dict(GCC_OPT, debugger="lldb", mode="dict", test="awaits", items={"6": "line"},
         reason="dict mode, GCC -O2: a co_await over several lines is shown at the closing brace."),
    dict(family="gcc", major=14, flavor="O2", debugger="lldb", mode="dict", test="awaits", items={"20": "line", "21": "line"},
         reason="dict mode, GCC 14 -O2: the spawn_tuple co_await is shown at the closing brace."),
    dict(family="gcc", major=14, flavor=("O2", "O2-zen"), debugger="lldb", mode="dict", test="threads", items={"threads": "bad-frame"},
         reason="dict mode, GCC 14 -O2: the co_await in main's lambda is shown at the closing brace."),
    dict(family="gcc", major=16, flavor="O2-zen", debugger="lldb", mode="dict", test="threads", items={"threads": "bad-frame"},
         reason="dict mode, GCC 16 -O2: the co_await in main's lambda is shown at the closing brace."),
    dict(family="clang", major=lambda v: v >= 20, flavor="O2", debugger="lldb", test="awaits", items={"30": "line"},
         reason="LLDB, Clang -O2: a non-TMC coroutine that reached its co_await inside its ramp "
                "(eager start) is shown at its first line: that index store is in the ramp."),
    dict(family="clang", major=20, flavor="O2", debugger="gdb", test="awaits", items={"30": "line"},
         reason="GDB, Clang 20 -O2 (no resume labels): the same eager non-TMC coroutine has no line."),
    dict(family="clang", major=lambda v: v <= 19, flavor="O2", items={"*": "no-async"},
         reason="Clang <= 19 emits no coroutine frame debug info when optimizing."),
]


def known_failures(job, stops):
    """The failures listed in KNOWN for this job: ({item: category}, [reasons])."""
    expected, reasons = {}, []
    c = job.build.compiler
    actual = {"debugger": job.debugger, "mode": job.mode, "test": job.test, "family": c.family, "major": c.major, "flavor": job.build.flavor}
    for entry in KNOWN:
        for field, value in actual.items():
            want = entry.get(field)
            if want is None:
                continue
            if callable(want):
                if not want(value):
                    break
            elif value not in (want if isinstance(want, (tuple, list, set)) else (want,)):
                break
        else:
            for item, category in entry["items"].items():
                for key in (stops if item == "*" else [item]):
                    if key in stops or item != "*" and not key.isdigit():
                        expected[key] = category
            reasons.append(entry["reason"])
    return expected, reasons


# ---------------------------------------------------------------------------
# Source anchors
# ---------------------------------------------------------------------------

class Anchors:
    """Line numbers of the `// @name` comments of the test programs."""

    def __init__(self, tmc_include):
        self.lines = {}
        for program in PROGRAMS:
            found = {}
            for number, text in enumerate((HERE / "programs" / (program + ".cpp")).read_text().splitlines(), 1):
                for name in re.findall(r"@(\w+)", text.partition("//")[2]):
                    found.setdefault(name, []).append(number)
            for name, numbers in found.items():
                last = found.get(name + "_end")
                if last:  # a co_await expression over several lines: any of its lines
                    numbers = list(range(min(numbers), max(last) + 1))
                self.lines[(program, name)] = set(numbers)
        self.client_main = self._client_main(tmc_include / "tmc" / "detail" / "ex_cpu.ipp")

    @staticmethod
    def _client_main(path):
        """The lines of the co_await expression in tmc::detail::client_main_awaiter."""
        text = path.read_text().splitlines()
        start = next(i for i, line in enumerate(text) if re.search(r"\bclient_main_awaiter\(", line))
        first = next(i for i in range(start, len(text)) if "co_await" in text[i])
        last = next(i for i in range(first, len(text)) if ";" in text[i])
        return set(range(first + 1, last + 2))

    def where(self, program, site):
        if site is CLIENT_MAIN:
            return "ex_cpu.ipp", self.client_main
        return program + ".cpp", self.lines[(program, site.anchor)]


# ---------------------------------------------------------------------------
# Judging observations
# ---------------------------------------------------------------------------

class Judge:
    def __init__(self, job, anchors, program):
        self.anchors = anchors
        self.program = program
        # LLDB's dict frames have no [fork] marker and LLDB's own names.
        self.shows_fork = job.mode != "dict"
        self.exact_gcc_names = job.build.compiler.family == "gcc" and job.debugger in ("lldb", "dap") and job.mode == "scripted"

    def at(self, frame, site):
        filename, lines = self.anchors.where(self.program, site)
        return frame["file"] == filename and frame["line"] in lines

    def named(self, frame, site):
        if self.exact_gcc_names:
            return frame["name"] == site.gcc_name
        return site.function in frame["name"]

    def chain(self, frames, sites):
        """The failure category of one async stack, or None."""
        if not frames:
            return "no-async"
        if not self.at(frames[0], sites[0]):
            return "line"
        if len(frames) != len(sites) or not all(self.at(f, s) for f, s in zip(frames, sites)):
            return "chain"
        if not all(self.named(f, s) for f, s in zip(frames, sites)):
            return "name"
        if self.shows_fork and any(f["fork"] != s.fork for f, s in zip(frames, sites)):
            return "fork"
        if any(f.get("expr") is False for f in frames):
            return "expr"
        return None

    def is_site(self, frame, site):
        return self.at(frame, site) and self.named(frame, site) and (not self.shows_fork or frame["fork"] == site.fork)


def add(observed, item, category):
    item = str(item)
    if category and category not in observed.get(item, "").split("+"):
        observed[item] = "+".join(sorted(filter(None, observed.get(item, "").split("+") + [category])))


# ---------------------------------------------------------------------------
# Running the probes
# ---------------------------------------------------------------------------

class Runner:
    def __init__(self, args, anchors):
        self.args = args
        self.anchors = anchors
        self.results = args.build_dir / "results"

    def paths(self, job, suffix=""):
        name = "-".join(filter(None, [job.debugger, job.mode, job.test])) + suffix
        directory = self.results / job.build.label
        directory.mkdir(parents=True, exist_ok=True)
        return directory / (name + ".json"), directory / (name + ".log")

    def binary(self, job, program):
        return str(job.build.dir / program)

    def launch(self, job, program, params, suffix=""):
        """Run one probe. Returns (observation, failure category): one of them is None."""
        out, log = self.paths(job, suffix)
        out.unlink(missing_ok=True)
        args = self.args
        params = dict(params, out=str(out), binary=self.binary(job, program))
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        env.pop("CORO_BT_FRAME_MODE", None)
        if job.debugger == "gdb":
            params["script"] = str(args.gdb_script)
            command = [args.gdb, "--batch", "-nx", "-x", str(HERE / "gdb_probe.py")]
        elif job.debugger == "lldb":
            params["script"] = str(args.lldb_script)
            env["CORO_BT_FRAME_MODE"] = job.mode
            command = [args.lldb, "--batch", "--no-lldbinit", "-o", "command script import %s" % (HERE / "lldb_probe.py"), "-o", "cbt_probe"]
        else:
            params["script"] = str(args.lldb_script)
            params["lldb_dap"] = args.lldb_dap
            env["CORO_BT_FRAME_MODE"] = job.mode
            command = [sys.executable, str(HERE / "dap_probe.py")]
        env["CBT_PARAMS"] = json.dumps(params)
        return self.execute(command, env, out, log)

    def execute(self, command, env, out, log):
        with open(log, "w") as log_file:
            try:
                subprocess.run(command, env=env, cwd=log.parent, stdin=subprocess.DEVNULL, stdout=log_file, stderr=subprocess.STDOUT, timeout=self.args.timeout)
            except subprocess.TimeoutExpired:
                return None, "timeout"
        text = log.read_text(errors="replace")
        if out is None:
            return text, None
        try:
            observation = json.loads(out.read_text())
        except (OSError, ValueError):
            return None, "crash"  # the debugger died before the probe could write
        if not observation["completed"] or observation["errors"]:
            return None, "probe-error"
        if "Python Exception" in text or "Traceback (most recent call last)" in text:
            return None, "python-error"
        return observation, None

    # ---- tests ------------------------------------------------------------------------------

    def chains(self, job, program, table, stops, params, suffix=""):
        """Stop in stop_here(tag) and compare every async stack with its expected chain."""
        observation, failure = self.launch(job, program, dict(params, **{"break": "stop_here"}), suffix)
        if failure:
            return {"run": failure}, observation
        judge = Judge(job, self.anchors, program)
        observed, seen = {}, Counter()
        for stop in observation["stops"]:
            seen[stop["tag"]] += 1
            if stop["tag"] in table:
                add(observed, stop["tag"], judge.chain(stop["frames"], table[stop["tag"]]))
            else:
                add(observed, stop["tag"], "unexpected-stop")
        for tag, count in stops.items():
            if seen[tag] != count:
                add(observed, tag, "missing-stop")
        return observed, observation

    def test_awaits(self, job):
        return self.chains(job, "awaits", AWAITS_GDB if job.debugger == "gdb" else AWAITS, AWAITS_STOPS, {})[0]

    def test_member(self, job):
        # LLDB: also evaluate an expression on every async frame (null `this`, null variable list).
        return self.chains(job, "member", MEMBER, MEMBER_STOPS, {"expr": 8})[0]

    def test_edge(self, job):
        """The member program again, with settings that used to break the scripts."""
        if job.debugger == "gdb":
            return self.chains(job, "member", MEMBER, MEMBER_STOPS, {"twice": True, "intel": True})[0]
        observed, observation = self.chains(job, "member", MEMBER, MEMBER_STOPS, {"early_import": True, "intel": True})
        if observation and "no target yet" not in self.paths(job)[1].read_text(errors="replace"):
            add(observed, "early-import", "bad-output")
        return observed

    def test_cycle(self, job):
        """A continuation chain that loops back on itself must still end."""
        observation, failure = self.launch(job, "member", {"break": "stop_here", "cycle": True})
        if failure:
            return {"run": failure}
        frames = observation["stops"][0]["frames"]
        judge = Judge(job, self.anchors, "member")
        distinct = len({(f["name"], f["line"]) for f in frames}) == len(frames)
        if not frames or not judge.at(frames[0], MEMBER[1][0]) or len(frames) > len(MEMBER[1]) or not distinct:
            return {"1": "bad-chain"}
        return {}

    def test_core(self, job):
        """The async stack read from a core file must be the one of the live process."""
        core = self.paths(job, "-save")[0].with_suffix(".core")
        core.unlink(missing_ok=True)
        try:
            live, failure = self.launch(job, "member", {"break": "stop_here", "save_core": str(core)}, "-save")
            if failure:
                return {"run": failure}
            if not core.exists():
                return {"run": "no-core"}
            observed, observation = self.chains(job, "member", MEMBER, {1: 1}, {"core": str(core)})
            if observation and observation["stops"] and not observed:
                strip = lambda frames: [(f["name"], f["file"], f["line"], f["fork"]) for f in frames]
                if strip(observation["stops"][0]["frames"]) != strip(live["stops"][0]["frames"]):
                    add(observed, 1, "differs-from-live")
            return observed
        finally:
            core.unlink(missing_ok=True)

    def test_printer(self, job):
        """GDB: the std::coroutine_handle pretty printer and $get_coro_promise."""
        observation, failure = self.launch(job, "awaits", {"break": "stop_here", "only_tags": [30], "print_at": {"30": list(PRINTED)}})
        if failure:
            return {"run": failure}
        printed = observation["extra"].get("printed", {})
        return {expr: "bad-output" for expr, wanted in PRINTED.items() if not all(w in printed.get(expr, "") for w in wanted)}

    def test_mi(self, job):
        """GDB/MI, which is what VS Code's C/C++ extension reads."""
        _, log = self.paths(job)
        commands = [
            "set debuginfod enabled off", "set confirm off", "file " + self.binary(job, "member"), "source %s" % self.args.gdb_script,
            'interpreter-exec mi "-enable-frame-filters"', "break stop_here", "run", 'interpreter-exec mi "-stack-list-frames"', "kill",
        ]
        command = [self.args.gdb, "--batch", "-nx"] + [x for c in commands for x in ("-ex", c)]
        text, failure = self.execute(command, dict(os.environ, PYTHONDONTWRITEBYTECODE="1"), None, log)
        if failure:
            return {"run": failure}
        if "Python Exception" in text:
            return {"run": "python-error"}
        frames = []
        for frame in parse_mi_frames(next((line for line in text.splitlines() if line.startswith("^done,stack=")), "")):
            name = frame.get("func", "")
            fork = name.startswith("[fork] ")
            name = name[len("[fork] "):] if fork else name
            if name.startswith("[async] "):
                frames.append({"name": name[len("[async] "):], "fork": fork, "file": os.path.basename(frame.get("file", "")), "line": int(frame.get("line", 0))})
        observed = {}
        add(observed, 1, Judge(job, self.anchors, "member").chain(frames, MEMBER[1]))
        return observed

    def test_threads(self, job):
        """8 worker threads: no crash, and every thread's async stack is a plausible one."""
        params = {"break": "stop_here", "ignore": 300, "max_stops": 1, "all_threads": True, "tags": False, "expr": 6}
        judge = Judge(job, self.anchors, "threads")
        observed = {}
        # Several runs: where the other threads are stopped differs every time.
        for run in range(self.args.threads_repeat if job.debugger == "lldb" else 1):
            observation, failure = self.launch(job, "threads", params, "-%d" % run)
            if failure:
                add(observed, "threads", failure)
                continue
            stop = observation["stops"][0]
            stacks = [stop["frames"]] + stop["others"]
            if not any(stacks):
                add(observed, "threads", "no-async")
            for frames in stacks:
                rest = list(frames)
                while rest and any(judge.is_site(rest[0], site) for site in THREADS_FIB):
                    rest.pop(0)
                if rest and not (len(rest) == len(THREADS_TAIL) and all(judge.is_site(f, s) for f, s in zip(rest, THREADS_TAIL))):
                    add(observed, "threads", "bad-frame")
                if any(f.get("expr") is False for f in frames):
                    add(observed, "threads", "expr")
        return observed

    def test_dap(self, job):
        """lldb-dap, which is what VS Code's LLDB DAP extension talks to: names, [fork], grey frames, Watch."""
        observation, failure = self.launch(job, "awaits", {"tag": 20})
        if failure:
            return {"run": failure}
        observed = {}
        add(observed, 20, Judge(job, self.anchors, "awaits").chain(observation["stops"][0]["frames"], AWAITS[20]))
        return observed

    # The stops of each test: what "*" in KNOWN stands for, and which tags an entry can name.
    STOPS = {
        "awaits": [str(t) for t in AWAITS_STOPS], "member": ["1", "2"], "edge": ["1", "2"], "cycle": ["1"],
        "core": ["1"], "printer": [], "mi": ["1"], "threads": ["threads"], "dap": ["20"],
    }

    def run(self, job):
        try:
            observed = getattr(self, "test_" + job.test)(job)
        except Exception as e:  # a bug in this file or a probe, not a result
            observed = {"run": "runner-error: %s: %s" % (type(e).__name__, e)}
        expected, reasons = known_failures(job, self.STOPS[job.test])
        if observed == expected:
            status = "XFAIL" if expected else "PASS"
        elif all(expected.get(item) == category for item, category in observed.items()):
            status = "XPASS"  # fewer failures than listed in KNOWN
        else:
            status = "FAIL"
        return job, status, observed, expected, reasons


def parse_mi_frames(text):
    """The frame={...} tuples of an MI -stack-list-frames answer, as dicts."""
    pair = re.compile(r',?([\w-]+)="((?:[^"\\]|\\.)*)"')
    frames, pos = [], 0
    while True:
        pos = text.find("frame={", pos)
        if pos < 0:
            return frames
        pos += len("frame={")
        frame = {}
        while True:
            match = pair.match(text, pos)
            if not match:
                break
            frame[match.group(1)] = match.group(2)
            pos = match.end()
        frames.append(frame)


# ---------------------------------------------------------------------------
# Compilers and builds
# ---------------------------------------------------------------------------

def find_compilers(names):
    if not names:
        names = set()
        for directory in os.environ.get("PATH", "").split(os.pathsep):
            if os.path.isdir(directory):
                names.update(n for n in os.listdir(directory) if re.fullmatch(r"(clang\+\+|g\+\+)(-\d+)?", n))
        names = sorted(names)
    found = {}
    for name in names:
        exe = shutil.which(name)
        if not exe:
            sys.exit("compiler not found: " + name)
        text = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout
        version = re.search(r"(\d+)\.\d+\.\d+", text)
        if not version:
            continue
        family = "clang" if "clang" in text.lower() else "gcc"
        # `clang++` and `clang++-21` are often the same compiler: keep one.
        found.setdefault((family, version.group(0)), Compiler(exe, family, int(version.group(1)), version.group(0)))
    return sorted(found.values(), key=lambda c: (c.family, c.major))


def newest_mtime(directory):
    return max(p.stat().st_mtime for p in directory.rglob("*") if p.is_file())


def build_program(args, build, program, sources_mtime):
    source = HERE / "programs" / (program + ".cpp")
    target = build.dir / program
    if not args.rebuild and target.exists() and target.stat().st_mtime > max(sources_mtime, source.stat().st_mtime):
        return None
    build.dir.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c++20", "-g", "-pthread", "-I", str(args.tmc_include)] + FLAVORS[build.flavor][0]
    result = subprocess.run([build.compiler.exe] + flags + args.cxxflags.split() + ["-o", str(target), str(source)], capture_output=True, text=True)
    if result.returncode != 0:
        return "%s %s: %s" % (build.label, program, result.stderr.strip().splitlines()[-1] if result.stderr.strip() else "compiler failed")
    return None


def tool_version(exe):
    try:
        lines = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout.splitlines()
        return lines[0].strip() if lines else "?"
    except OSError:
        return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def tests_for(build, debugger, mode):
    if debugger == "gdb":
        tests = ["awaits", "member", "edge", "cycle", "core", "printer", "mi", "threads"]
    elif debugger == "lldb":
        tests = ["awaits", "member", "edge", "cycle", "core", "threads"]
    else:
        tests = ["dap"]
    if build.flavor != "O0" and "cycle" in tests:
        tests.remove("cycle")  # the probe needs the -O0 variable names
    return tests


def default_tmc_include():
    candidates = [ROOT / "submodules" / "TooManyCooks" / "include"] + sorted(ROOT.glob("build/*/_deps/*/include"))
    return next((c for c in candidates if (c / "tmc" / "all_headers.hpp").exists()), candidates[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    csv = lambda text: [x for x in text.split(",") if x]
    parser.add_argument("--compilers", type=csv, default=[], help="comma-separated compiler executables (default: every clang++[-N] and g++[-N] on PATH)")
    parser.add_argument("--flavors", type=csv, default=list(FLAVORS), help="build flavors: " + ",".join(FLAVORS))
    parser.add_argument("--debuggers", type=csv, default=["gdb", "lldb", "dap"], help="gdb,lldb,dap (dap = lldb-dap)")
    parser.add_argument("--modes", type=csv, default=["scripted", "dict"], help="LLDB frame modes: scripted,dict")
    parser.add_argument("--tests", type=csv, default=[], help="only these tests: awaits,member,edge,cycle,core,printer,mi,threads,dap")
    parser.add_argument("--threads-repeat", type=int, default=4, help="LLDB runs of the threads test per build (default 4)")
    parser.add_argument("-j", "--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)), help="parallel jobs")
    parser.add_argument("--timeout", type=int, default=300, help="seconds per debugger run")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "coro_backtrace")
    parser.add_argument("--rebuild", action="store_true", help="rebuild the test programs even if they look current")
    parser.add_argument("--cxxflags", default="", help="extra compiler flags")
    parser.add_argument("--tmc-include", type=Path, default=default_tmc_include(), help="TooManyCooks include directory")
    parser.add_argument("--gdb-script", type=Path, default=ROOT / "coro_backtrace_gdb.py")
    parser.add_argument("--lldb-script", type=Path, default=ROOT / "coro_backtrace_lldb.py")
    parser.add_argument("--gdb", default="gdb")
    parser.add_argument("--lldb", default="lldb")
    parser.add_argument("--lldb-dap", default="lldb-dap")
    args = parser.parse_args()
    for name in ("build_dir", "tmc_include", "gdb_script", "lldb_script"):
        setattr(args, name, getattr(args, name).resolve())
    if not (args.tmc_include / "tmc" / "all_headers.hpp").exists():
        sys.exit("TooManyCooks headers not found in %s (use --tmc-include)" % args.tmc_include)

    # Debuggers
    debuggers = []
    for debugger, exe in (("gdb", args.gdb), ("lldb", args.lldb), ("dap", args.lldb_dap)):
        if debugger not in args.debuggers:
            continue
        path = shutil.which(exe)
        if not path:
            print("skipping %s: %s not found" % (debugger, exe))
            continue
        setattr(args, {"gdb": "gdb", "lldb": "lldb", "dap": "lldb_dap"}[debugger], path)
        version = tool_version(path)
        if debugger == "lldb":
            major = re.search(r"version (\d+)", version or "")
            if major and int(major.group(1)) < 22:
                print("skipping lldb: coro_backtrace_lldb.py needs LLDB 22 or newer (%s)" % version)
                continue
        print("%-5s %s" % (debugger, version))
        debuggers.append(debugger)

    # Builds
    compilers = find_compilers(args.compilers)
    if not compilers:
        sys.exit("no compilers found")
    builds = []
    for compiler in compilers:
        print("%-5s %s (%s)" % (compiler.family, compiler.version, compiler.exe))
        for flavor in args.flavors:
            if FLAVORS[flavor][1] in (None, compiler.family):
                label = "%s%d-%s" % (compiler.family, compiler.major, flavor)
                builds.append(Build(compiler, flavor, label, args.build_dir / label))
    sources_mtime = max(newest_mtime(args.tmc_include), Path(__file__).stat().st_mtime)
    broken = set()
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = {pool.submit(build_program, args, b, p, sources_mtime): b for b in builds for p in PROGRAMS}
        for future in concurrent.futures.as_completed(futures):
            if future.result():
                print("BUILD FAILED " + future.result())
                broken.add(futures[future].label)

    # Tests
    anchors = Anchors(args.tmc_include)
    runner = Runner(args, anchors)
    jobs = []
    for build in builds:
        if build.label in broken:
            continue
        for debugger in debuggers:
            for mode in (args.modes if debugger == "lldb" else ["scripted"] if debugger == "dap" else [None]):
                for test in tests_for(build, debugger, mode):
                    if not args.tests or test in args.tests:
                        jobs.append(Job(build, debugger, mode, test))
    print("\n%d builds, %d tests, %d at a time. Logs: %s\n" % (len(builds) - len(broken), len(jobs), args.jobs, runner.results))
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(runner.run, jobs))

    # Report
    by_row = {}
    for job, status, observed, expected, reasons in results:
        row = (job.build.label, "lldb-dap" if job.debugger == "dap" else job.debugger + (" " + job.mode if job.mode else ""))
        by_row.setdefault(row, []).append("%s=%s" % (job.test, status))
    last = None
    for (label, debugger), cells in by_row.items():
        print("%-11s %-14s %s" % (label if label != last else "", debugger, " ".join(cells)))
        last = label
    print()
    for job, status, observed, expected, reasons in results:
        if status == "PASS":
            continue
        show = lambda items: ", ".join("%s: %s" % kv for kv in sorted(items.items())) or "nothing"
        name = " ".join(filter(None, [job.build.label, job.debugger, job.mode, job.test]))
        if status == "XFAIL":
            print("XFAIL %s: %s\n      %s" % (name, show(observed), "\n      ".join(reasons)))
        else:
            print("%-5s %s\n      observed: %s\n      known:    %s\n      logs:     %s" % (
                status, name, show(observed), show(expected), str(runner.paths(job)[1]).replace(".log", "*")))
    counts = Counter(status for _, status, _, _, _ in results)
    print("\n" + ", ".join("%d %s" % (counts[s], s) for s in ("PASS", "XFAIL", "XPASS", "FAIL") if counts[s]) + (", %d builds failed" % len(broken) if broken else ""))
    if counts["XPASS"]:
        print("XPASS: a known limitation did not show. Update KNOWN in %s." % Path(__file__).name)
    return 1 if counts["FAIL"] or broken else 0


if __name__ == "__main__":
    sys.exit(main())
