#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print the section of CHANGELOG.md for a version: the lines after its "## <version>" heading up
to the next "## " heading, without the blank lines at either end. These are the notes of the
release.

The heading may be "## 1.0.0", "## v1.0.0" or "## [1.0.0]", followed by nothing or by a date. A
version without a section is an error (the changelog is written before the tag is). With
--allow-unreleased the section "## Unreleased" stands in for it, which is what a rehearsal of the
release uses on a branch where the version has not been given its heading yet.

  changelog-section.py <version> [--allow-unreleased] [--changelog CHANGELOG.md]
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def sections(lines):
    """[(heading text, [body lines])] for each "## " heading."""
    found = []
    for line in lines:
        if line.startswith("## "):
            found.append((line[3:].strip(), []))
        elif found:
            found[-1][1].append(line)
    return found


def heading_is(heading, version):
    return re.match(r"^\[?v?%s\]?(\s|$)" % re.escape(version), heading) is not None


def find_section(lines, version, allow_unreleased):
    parts = sections(lines)
    for heading, body in parts:
        if heading_is(heading, version):
            return body
    if allow_unreleased:
        for heading, body in parts:
            if heading.lower() == "unreleased":
                return body
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(description="print the changelog section of a version")
    parser.add_argument("version")
    parser.add_argument("--allow-unreleased", action="store_true",
                        help='use the "Unreleased" section when the version has none')
    parser.add_argument("--changelog", default=os.path.join(ROOT, "CHANGELOG.md"))
    args = parser.parse_args(argv)

    with open(args.changelog, encoding="utf-8") as handle:
        lines = handle.read().splitlines()
    body = find_section(lines, args.version, args.allow_unreleased)
    if body is None:
        print("changelog-section.py: %s has no section \"## %s\"" % (args.changelog, args.version), file=sys.stderr)
        return 1
    text = "\n".join(body).strip()
    if not text:
        print("changelog-section.py: the section of %s in %s is empty" % (args.version, args.changelog), file=sys.stderr)
        return 1
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
