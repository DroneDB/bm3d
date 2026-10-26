#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List the files of the third-party source trees that a build really reads.

The input is what the compilers recorded while building:
  * GCC / Clang dependency files (-MD), given with --depfile or found under --depfile-dir
    (every *.d file below it);
  * MSVC /showIncludes output saved to a file (--msvc-log), whose "Note: including file:"
    lines are read. The union of all inputs is reported, so a list made on Linux and a list
    made on Windows can be merged.

A component is a name and a source root (--root name=path). A file belongs to the component
with the deepest root that contains it, so a root may lie inside another one (the corto
sources sit below the Nexus sources in the upstream tree). Everything outside every root is
dropped: system headers, vcpkg ports, the project's own files.

Output: one section per component, sorted, paths relative to the component root and with
forward slashes. Re-run it against this repository's own build to find out which imported
files are still needed, e.g.

  scripts/closure.py --depfile-dir build --root vcg=third_party/vcg \\
      --root corto=third_party/corto --root bm3d=src
"""
import argparse
import ntpath
import os
import posixpath
import re
import sys



def parse_depfile(text):
    """Return the dependency paths of one Makefile-style depfile (first rule)."""
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    # Drop the target: it ends at the first unescaped colon followed by whitespace.
    match = re.search(r"(?<!\\):(?=\s)", text)
    if match is None:
        return []
    deps = []
    current = []
    chars = text[match.end():]
    i = 0
    while i < len(chars):
        c = chars[i]
        if c == "\\" and i + 1 < len(chars) and chars[i + 1] in " #:\\":
            current.append(chars[i + 1])
            i += 2
            continue
        if c in " \t\r\n":
            if current:
                deps.append("".join(current))
                current = []
        else:
            current.append(c)
        i += 1
    if current:
        deps.append("".join(current))
    # Depfiles written without -MP have no phony targets; with -MP further rules follow,
    # and their "dep:" lines would show up here as trailing names ending with a colon.
    return [d for d in deps if not d.endswith(":")]


def parse_msvc_log(text):
    """Return the paths of the "Note: including file:" lines (any message language)."""
    paths = []
    for line in text.splitlines():
        if "including file" in line or "file incluso" in line.lower():
            _, _, rest = line.partition(": ")
            _, _, rest2 = rest.partition(": ")
            candidate = (rest2 or rest).strip()
            if candidate:
                paths.append(candidate)
    return paths


def is_windows_path(path):
    return bool(re.match(r"^[A-Za-z]:[\\/]", path)) or "\\" in path


def normalise(path):
    """Normalise a path from either platform to forward slashes, without resolving links."""
    if is_windows_path(path):
        return ntpath.normpath(path).replace("\\", "/")
    return posixpath.normpath(path)


class Roots:
    def __init__(self, roots):
        self.items = []
        for name, path in roots:
            if is_windows_path(path):
                real = path    # a root named as on the Windows machine that wrote the log
            else:
                real = os.path.realpath(path) if os.path.exists(path) else os.path.abspath(path)
            self.items.append((name, normalise(real).rstrip("/")))
        # Deepest root first, so nested roots win.
        self.items.sort(key=lambda item: -len(item[1]))

    def locate(self, path):
        """Return (component, relative path) or None."""
        windows = is_windows_path(path)
        if windows:
            path = normalise(path)
        else:
            absolute = path if os.path.isabs(path) else os.path.abspath(path)
            path = normalise(os.path.realpath(absolute) if os.path.exists(absolute) else absolute)
        for name, root in self.items:
            if windows:
                if path.lower().startswith(root.lower() + "/"):
                    return name, path[len(root) + 1:]
            elif path.startswith(root + "/"):
                return name, path[len(root) + 1:]
        return None


def collect(depfiles, msvc_logs, roots):
    """Return {component: sorted list of relative paths}."""
    found = {name: {} for name, _ in roots.items}  # casefolded -> first spelling seen

    def add(path):
        hit = roots.locate(path)
        if hit is not None:
            found[hit[0]].setdefault(hit[1].casefold(), hit[1])

    for depfile in depfiles:
        with open(depfile, encoding="utf-8", errors="replace") as handle:
            for dep in parse_depfile(handle.read()):
                add(dep)
    for log in msvc_logs:
        with open(log, encoding="utf-8", errors="replace") as handle:
            for dep in parse_msvc_log(handle.read()):
                add(dep)
    return {name: sorted(spellings.values()) for name, spellings in found.items()}


def find_depfiles(directory):
    for base, _, names in os.walk(directory):
        for name in names:
            if name.endswith(".d"):
                yield os.path.join(base, name)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--depfile", action="append", default=[], help="GCC/Clang depfile")
    parser.add_argument("--depfile-dir", action="append", default=[],
                        help="directory searched for *.d depfiles")
    parser.add_argument("--msvc-log", action="append", default=[],
                        help="saved MSVC /showIncludes output")
    parser.add_argument("--root", action="append", default=[], metavar="NAME=PATH",
                        help="component and its source root (repeatable)")
    parser.add_argument("--only", metavar="NAME", help="print the bare list of one component")
    parser.add_argument("--exclude", action="append", default=[], metavar="RELPATH",
                        help="leave a relative path out of the output (repeatable)")
    args = parser.parse_args(argv)

    if not args.root:
        parser.error("at least one --root NAME=PATH is required")
    roots = []
    for item in args.root:
        name, sep, path = item.partition("=")
        if not sep or not name or not path:
            parser.error("--root takes NAME=PATH, got %r" % item)
        roots.append((name, path))

    depfiles = list(args.depfile)
    for directory in args.depfile_dir:
        depfiles.extend(sorted(find_depfiles(directory)))
    if not depfiles and not args.msvc_log:
        parser.error("no input: give --depfile, --depfile-dir or --msvc-log")

    result = collect(depfiles, args.msvc_log, Roots(roots))
    excluded = {e.casefold() for e in args.exclude}
    result = {name: [p for p in paths if p.casefold() not in excluded]
              for name, paths in result.items()}

    if args.only:
        if args.only not in result:
            parser.error("unknown component %r" % args.only)
        sys.stdout.write("".join(p + "\n" for p in result[args.only]))
        return 0
    for name, paths in sorted(result.items()):
        print("[%s] %d files" % (name, len(paths)))
        for path in paths:
            print(path)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
