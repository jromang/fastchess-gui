#!/usr/bin/env python3
"""Runs doctest programs' test cases in parallel, each in a process of its own.

The tests mostly wait (for engines, servers, timeouts): run one after the other they keep a processor busy a tenth of
the time. Here they run side by side, the slowest of the last run started first; each process has its temporary
folder and its ports of its own (tournament/tests/own_temp.hpp, gui/tests/free_ports.hpp).

    tools/run_tests.py build/fastchess-gui-tests build/fcg-tournament-tests
    tools/run_tests.py -j 8 build/fcg-tournament-tests -sf='*runner*'

The options that follow the programs are given to doctest to choose the test cases (-tc, -ts, -sf, their -e forms...).
The time each test case took is kept next to its program (<program>.durations.json) for the next run's order.
"""

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed


def list_test_cases(program, filters):
    out = subprocess.run([program, "-r=xml", "-ltc", *filters], capture_output=True, check=True).stdout
    names = []
    for case in ET.fromstring(out).iter("TestCase"):
        name = case.get("name")
        if case.get("skipped") != "true" and name not in names:
            names.append(name)
    return names


def name_filter(name):
    # doctest splits its filters at commas and reads backslashes, unless escaped; * and ? are wildcards, which still
    # match themselves. Anything not ASCII as a wildcard: a Windows command line would not carry it as it is.
    escaped = name.replace("\\", "\\\\").replace(",", "\\,")
    return "-tc=" + re.sub(r"[^\x20-\x7e]+", "*", escaped)


def kill_tree(process):
    if os.name == "nt":
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True)
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_one(program, name, timeout):
    # Its name only: doctest keeps a single -tc, which the filters given would replace.
    command = [program, name_filter(name), "--no-intro=true", "--no-version=true"]
    start = time.monotonic()
    # A process group of its own (POSIX) or a tree (Windows), so that a test case that hangs goes with its engines.
    extra = {"start_new_session": True} if os.name != "nt" else {}
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                               **extra)
    try:
        output, _ = process.communicate(timeout=timeout)
        timed_out = False
    except subprocess.TimeoutExpired:
        kill_tree(process)
        output, _ = process.communicate()
        timed_out = True
    seconds = time.monotonic() - start
    text = output.decode("utf-8", "replace")
    ran = re.search(r"test cases:\s*(\d+)", text)
    if timed_out:
        problem = f"timed out after {timeout:g} s"
    elif process.returncode != 0:
        problem = f"exit code {process.returncode}"
    elif not ran or int(ran.group(1)) != 1:
        problem = f"its name matched {ran.group(1) if ran else 'no'} test cases instead of one"
    else:
        problem = None
    return seconds, problem, text


def load_durations(program):
    try:
        with open(program + ".durations.json", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4, help="test cases at once")
    parser.add_argument("--timeout", type=float, default=600, help="seconds a test case may take")
    parser.add_argument("programs", nargs="+", metavar="program")
    args, filters = parser.parse_known_args()
    # argparse takes an option with a space in it (-tc=a name*) for a positional argument: a filter all the same.
    filters += [p for p in args.programs if p.startswith("-")]
    programs = [os.path.abspath(p) for p in args.programs if not p.startswith("-")]

    durations = {p: load_durations(p) for p in programs}
    cases = [(p, n) for p in programs for n in list_test_cases(p, filters)]
    if not cases:
        print("run_tests: no test case to run")
        return 1
    # The slowest first, and those never timed before them: the last to start are then the quick ones.
    cases.sort(key=lambda c: -durations[c[0]].get(c[1], float("inf")))

    start = time.monotonic()
    failures = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {pool.submit(run_one, p, n, args.timeout): (p, n) for p, n in cases}
        for future in as_completed(futures):
            program, name = futures[future]
            seconds, problem, text = future.result()
            durations[program][name] = round(seconds, 3)
            if problem:
                failures.append(name)
                print(f"\n=== FAILED ({problem}, {seconds:.1f} s): {name}\n{text.rstrip()}\n", flush=True)
    wall = time.monotonic() - start

    for program in programs:
        try:
            with open(program + ".durations.json", "w", encoding="utf-8") as f:
                json.dump(durations[program], f, indent=0, sort_keys=True)
        except OSError:
            pass

    slowest = sorted(cases, key=lambda c: -durations[c[0]][c[1]])[:3]
    print(f"{len(cases) - len(failures)}/{len(cases)} test cases passed in {wall:.1f} s, {args.jobs} at once; slowest: "
          + "; ".join(f"{durations[p][n]:.1f} s {n}" for p, n in slowest))
    if failures:
        print("Failed:\n" + "\n".join("  " + n for n in failures))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
