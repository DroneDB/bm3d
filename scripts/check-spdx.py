#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when a source file written for this project has no SPDX-License-Identifier line.

Not checked, because their licence is the upstream one and is recorded elsewhere:
  * third_party/ (vcglib and corto, byte for byte), licenses/ (licence texts);
  * the Nexus files imported under src/ (listed in src/NEXUS_SOURCE, under the path they have
    now), whatever has changed in them since: they keep their upstream notice;
  * build directories at the top of the repository (build*, cmake-build*), .git, vcpkg trees,
    node_modules and the like. A folder of the sources called build/ is checked.

Checked: .cpp .cc .h .hpp .py .js .mjs .cmake .yml .yaml .sh and CMakeLists.txt. The line has to
be among the first 40 lines. Exit status 0 when every file has one, 1 otherwise (the files are
listed).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import syncutil  # noqa: E402

EXTENSIONS = (".cpp", ".cc", ".h", ".hpp", ".py", ".js", ".mjs", ".cmake", ".yml", ".yaml", ".sh")
NAMES = ("CMakeLists.txt",)
SKIP_DIR_NAMES = {".git", "third_party", "licenses", "node_modules", "vcpkg", "vcpkg_installed",
                  "__pycache__", "dist", "out"}
SKIP_DIR_PREFIXES = ("build", "cmake-build")
HEADER_LINES = 40
MARK = "SPDX-License-Identifier:"


def repository_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def imported_nexus_files(root):
    """Paths (relative to the repository root, forward slashes) of the files in src/NEXUS_SOURCE,
    where they are now."""
    path = os.path.join(root, "src", "NEXUS_SOURCE")
    if not os.path.exists(path):
        return set()
    manifest = syncutil.Manifest.read(path)
    return {"src/" + manifest.current_path(name) for name in manifest.files}


def candidates(root):
    for base, dirs, names in os.walk(root):
        at_top = os.path.samefile(base, root)
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIR_NAMES
                         and not (at_top and d.startswith(SKIP_DIR_PREFIXES)))
        for name in sorted(names):
            if name in NAMES or name.endswith(EXTENSIONS):
                path = os.path.join(base, name)
                yield os.path.relpath(path, root).replace(os.sep, "/")


def has_identifier(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        for _, line in zip(range(HEADER_LINES), handle):
            if MARK in line:
                return True
    return False


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", default=repository_root(), help="repository root (default: this one)")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root)
    imported = imported_nexus_files(root)
    checked = 0
    missing = []
    for relative in candidates(root):
        if relative in imported:
            continue
        checked += 1
        if not has_identifier(os.path.join(root, relative)):
            missing.append(relative)
    if missing:
        print("These files have no %s line in their first %d lines:" % (MARK, HEADER_LINES))
        for relative in missing:
            print("  " + relative)
        return 1
    print("SPDX: %d files checked, %d imported Nexus files skipped" % (checked, len(imported)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
