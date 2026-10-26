#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Time bm3d builds of one model with one or more programs and compare what they write.

  bench.py --model model.obj [--runs 3] [--threads 8] [--json out.json] [name=]bm3d [[name=]bm3d ...]

Every program builds the model --runs times with --threads threads. The runs of the programs are
interleaved (a, b, c, a, b, c, ...), so a slow minute of the machine falls on all of them and not
on one. For each program the table gives the median of the wall time (seconds), its spread
((max - min) / median), the peak resident memory of the build (the median of the runs), the size
of the file written, its SHA-256 and whether that is the SHA-256 of the first program's file. The
same summary goes to --json. The first program is the reference: the last column of the others is
their median time relative to it, which is what a change of image library is judged on (at most
3% slower, and the same bytes).

The load of the machine is read before the runs and after each of them: a run on a busy machine
means nothing. --max-load (4 by default) makes the script wait, polling every minute for up to 20
minutes, until the one-minute load average is below it.

Linux (the peak memory comes from wait4). Exit status 0 when every build succeeded, 1 when one
failed, 2 for a wrong command line.
"""
import argparse
import hashlib
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def load_average():
    return os.getloadavg()[0]


def wait_for_quiet(max_load, wait_minutes):
    """Wait until the one-minute load is below max_load; the load seen last is returned."""
    deadline = time.monotonic() + wait_minutes * 60
    load = load_average()
    while load >= max_load and time.monotonic() < deadline:
        print(f"load {load:.2f} >= {max_load}: waiting a minute", file=sys.stderr)
        time.sleep(60)
        load = load_average()
    return load


def run_once(program, model, output, threads):
    """One build: (exit status, wall seconds, peak resident memory in KiB, stderr text)."""
    start = time.monotonic()
    with tempfile.TemporaryFile() as errors:
        child = subprocess.Popen([program, "--threads", str(threads), model, output],
                                 stdout=subprocess.DEVNULL, stderr=errors)
        _, status, usage = os.wait4(child.pid, 0)
        wall = time.monotonic() - start
        child.returncode = os.waitstatus_to_exitcode(status)
        errors.seek(0)
        text = errors.read().decode("utf-8", "replace")
    return child.returncode, wall, usage.ru_maxrss, text


def summarise(name, runs):
    times = [r["seconds"] for r in runs]
    median = statistics.median(times)
    return {
        "name": name,
        "runs": len(runs),
        "seconds": [round(t, 2) for t in times],
        "median_seconds": round(median, 2),
        "spread_percent": round(100.0 * (max(times) - min(times)) / median, 1) if median else 0.0,
        "peak_rss_mib": round(statistics.median(r["rss_kib"] for r in runs) / 1024, 0),
        "bytes": runs[-1]["bytes"],
        "sha256": [r["sha256"] for r in runs],
        "deterministic": len({r["sha256"] for r in runs}) == 1,
        "load": [round(r["load"], 2) for r in runs],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("programs", nargs="+", metavar="[name=]bm3d")
    parser.add_argument("--model", required=True, help="the model to build (.obj, .ply or .stl)")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--json", help="write the summary here")
    parser.add_argument("--workdir", help="where the files are written (deleted after each run)")
    parser.add_argument("--max-load", type=float, default=4.0)
    parser.add_argument("--wait-minutes", type=float, default=20.0)
    args = parser.parse_args()
    if args.runs < 1 or args.threads < 1:
        parser.error("--runs and --threads must be at least 1")

    programs = []
    for text in args.programs:
        name, _, path = text.rpartition("=")
        programs.append((name or os.path.basename(os.path.dirname(os.path.abspath(path))) or path, path))
    for name, path in programs:
        if not os.access(path, os.X_OK):
            parser.error(f"{path} is not a program")
    if not os.path.isfile(args.model):
        parser.error(f"{args.model} is not a file")

    runs = {name: [] for name, _ in programs}
    failed = False
    load = wait_for_quiet(args.max_load, args.wait_minutes)
    if load >= args.max_load:
        print(f"the machine stayed busy (load {load:.2f}): the times below are not reliable", file=sys.stderr)
    print(f"load before the runs: {load:.2f}", file=sys.stderr)

    with tempfile.TemporaryDirectory(dir=args.workdir) as directory:
        output = os.path.join(directory, "out.bm3d")
        for turn in range(args.runs):
            for name, path in programs:
                status, wall, rss, text = run_once(path, args.model, output, args.threads)
                if status != 0:
                    print(f"{name}: run {turn + 1} failed with exit status {status}\n{text}", file=sys.stderr)
                    failed = True
                    continue
                runs[name].append({"seconds": wall, "rss_kib": rss, "bytes": os.path.getsize(output),
                                   "sha256": sha256(output), "load": load_average()})
                os.remove(output)
                print(f"{name} run {turn + 1}/{args.runs}: {wall:.1f} s, {rss / 1024:.0f} MiB, "
                      f"load {runs[name][-1]['load']:.2f}", file=sys.stderr)

    summaries = [summarise(name, runs[name]) for name, _ in programs if runs[name]]
    if summaries:
        reference = summaries[0]
        for summary in summaries:
            summary["same_as_first"] = summary["sha256"][0] == reference["sha256"][0] and summary["deterministic"]
            summary["relative_to_first_percent"] = round(
                100.0 * (summary["median_seconds"] / reference["median_seconds"] - 1.0), 1)

        print(f"{'program':<14}{'median s':>9}{'spread':>8}{'RSS MiB':>9}{'bytes':>13}  "
              f"{'sha256':<12}{'same':>6}{'vs first':>10}")
        for s in summaries:
            print(f"{s['name']:<14}{s['median_seconds']:>9.2f}{s['spread_percent']:>7.1f}%{s['peak_rss_mib']:>9.0f}"
                  f"{s['bytes']:>13}  {s['sha256'][0][:12]:<12}{'yes' if s['same_as_first'] else 'NO':>6}"
                  f"{s['relative_to_first_percent']:>+9.1f}%")
        document = {"model": args.model, "threads": args.threads, "runs": args.runs, "programs": summaries}
        print(json.dumps(document, indent=2))
        if args.json:
            with open(args.json, "w", encoding="utf-8") as handle:
                json.dump(document, handle, indent=2)
                handle.write("\n")
    return 1 if failed or not summaries else 0


if __name__ == "__main__":
    sys.exit(main())
