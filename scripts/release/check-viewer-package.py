#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail unless the tarball that npm pack made of web/ holds what a user of the viewer needs: the
two bundles, the decoder worker, corto.wasm, the licence, the notice and the README, and a
package.json that names the viewer and the version of the release.

  check-viewer-package.py <bm3d-viewer-<version>.tgz> --version 1.0.0
"""
import argparse
import json
import sys
import tarfile

REQUIRED = ("dist/bm3d-viewer.js", "dist/bm3d-viewer.min.js", "dist/corto-worker.js", "dist/corto.wasm",
            "LICENSE", "NOTICE", "README.md", "package.json")


def main(argv=None):
    parser = argparse.ArgumentParser(description="check the tarball of the viewer")
    parser.add_argument("tarball")
    parser.add_argument("--version", required=True)
    args = parser.parse_args(argv)

    with tarfile.open(args.tarball, "r:gz") as archive:
        sizes = {m.name: m.size for m in archive.getmembers() if m.isfile()}
        problems = []
        for name in REQUIRED:
            if "package/" + name not in sizes:
                problems.append("missing: " + name)
            elif sizes["package/" + name] == 0:
                problems.append("empty: " + name)
        if "package/package.json" in sizes:
            package = json.load(archive.extractfile("package/package.json"))
            if package.get("name") != "bm3d-viewer" or package.get("version") != args.version:
                problems.append("package.json is %s@%s, expected bm3d-viewer@%s"
                                % (package.get("name"), package.get("version"), args.version))
    for problem in problems:
        print("check-viewer-package.py: %s: %s" % (args.tarball, problem), file=sys.stderr)
    if not problems:
        for name in sorted(sizes):
            print("  %s (%d bytes)" % (name, sizes[name]))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
