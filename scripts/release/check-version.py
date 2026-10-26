#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail unless every place that holds the version of the release says the same one.

The places are VERSION (the source of truth), "version" in vcpkg.json, in web/package.json and in
web/package-lock.json (the package itself, twice), and, with --tag, the name of the git tag, which
must be "v" followed by the version. Prints the version when they agree, so that a workflow can
take it from the output.

  check-version.py [--tag v1.0.0]
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def read_json(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return json.load(handle)


def versions():
    """(where, version) for each place, VERSION first."""
    with open(os.path.join(ROOT, "VERSION"), encoding="utf-8") as handle:
        found = [("VERSION", handle.readline().strip())]
    found.append(("vcpkg.json", read_json("vcpkg.json").get("version")))
    found.append(("web/package.json", read_json("web", "package.json").get("version")))
    lock = read_json("web", "package-lock.json")
    found.append(("web/package-lock.json", lock.get("version")))
    found.append(("web/package-lock.json (packages[\"\"])", lock.get("packages", {}).get("", {}).get("version")))
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description="check that the version of the release is the same everywhere")
    parser.add_argument("--tag", help="the git tag of the release; it has to be v<VERSION>")
    args = parser.parse_args(argv)

    found = versions()
    expected = found[0][1]
    problems = ["%s says %r, VERSION says %r" % (where, value, expected)
                for where, value in found[1:] if value != expected]
    if not expected:
        problems.append("VERSION is empty")
    if args.tag is not None and args.tag != "v" + expected:
        problems.append("the tag is %r but VERSION says %r, so the tag has to be %r" % (args.tag, expected, "v" + expected))
    if problems:
        for problem in problems:
            print("check-version.py: " + problem, file=sys.stderr)
        return 1
    print(expected)
    return 0


if __name__ == "__main__":
    sys.exit(main())
