#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail unless the program of a release links nothing but the system.

Linux: the shared libraries the program needs (DT_NEEDED, from objdump -p) have to be among the
ones every glibc system has (libc, libm, libpthread, libdl, librt, the loader), so libstdc++ and
libgcc are in the program; and no symbol may need a glibc version newer than --max-glibc (the
release is built on Ubuntu 22.04, glibc 2.35, so that it runs on systems from then on).

Windows: the DLLs the program imports (dumpbin /dependents) must not be a C or C++ runtime of
Visual Studio (vcruntime, msvcp, ucrtbase, the api-ms-win-crt forwarders, concrt, msvcr), which
is what a static runtime (/MT) leaves out.

Prints the libraries found and exits 1 on the first kind of problem it finds.

  check-linkage.py <program> [--max-glibc 2.35]
"""
import argparse
import re
import subprocess
import sys

LINUX_ALLOWED = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
                 "ld-linux-x86-64.so.2"}
WINDOWS_FORBIDDEN = re.compile(r"^(vcruntime|msvcp|msvcr|ucrtbase|concrt|api-ms-win-crt-|vccorlib)", re.IGNORECASE)


def run(command):
    return subprocess.run(command, check=True, stdout=subprocess.PIPE, universal_newlines=True).stdout


def linux_needed(program):
    return re.findall(r"^\s*NEEDED\s+(\S+)", run(["objdump", "-p", program]), re.MULTILINE)


def newest_glibc(program):
    """The highest GLIBC_x.y a symbol of the program asks for, as a tuple, or None."""
    versions = re.findall(r"GLIBC_(\d+(?:\.\d+)+)", run(["objdump", "-T", program]))
    return max((tuple(int(n) for n in v.split(".")) for v in versions), default=None)


def windows_imports(program):
    text = run(["dumpbin", "/dependents", program])
    return re.findall(r"^\s+(\S+\.dll)\s*$", text, re.MULTILINE | re.IGNORECASE)


def check_linux(program, max_glibc):
    needed = linux_needed(program)
    print("needs: " + ", ".join(needed))
    problems = ["links %s, which is not part of every glibc system" % name
                for name in needed if name not in LINUX_ALLOWED]
    newest = newest_glibc(program)
    limit = tuple(int(n) for n in max_glibc.split("."))
    print("newest glibc symbol version: " + (".".join(map(str, newest)) if newest else "none"))
    if newest and newest > limit:
        problems.append("needs glibc %s, newer than %s" % (".".join(map(str, newest)), max_glibc))
    return problems


def check_windows(program):
    imports = windows_imports(program)
    print("imports: " + ", ".join(imports))
    if not imports:
        return ["dumpbin listed no imported DLL: the output was not understood"]
    return ["imports %s, a C or C++ runtime that the program should contain" % name
            for name in imports if WINDOWS_FORBIDDEN.match(name)]


def main(argv=None):
    parser = argparse.ArgumentParser(description="check that a release program links only the system")
    parser.add_argument("program")
    parser.add_argument("--max-glibc", default="2.35")
    args = parser.parse_args(argv)
    problems = check_windows(args.program) if args.program.lower().endswith(".exe") \
        else check_linux(args.program, args.max_glibc)
    for problem in problems:
        print("check-linkage.py: %s %s" % (args.program, problem), file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
