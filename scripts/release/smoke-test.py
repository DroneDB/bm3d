#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the program of a release the way a user does: it has to say the version, and to build a
model (a grid of 2048 triangles, written here as an OBJ) into a file that starts with the magic
number of the format, "BM3D". The test programs are not built for a release, so this is the only
run of the program that was made for the archive.

  smoke-test.py <program> --version 1.0.0
"""
import argparse
import os
import subprocess
import sys
import tempfile

GRID = 32  # squares per side; two triangles each


def write_grid(path):
    """A bumpy square of GRID x GRID squares, so that the simplification has something to do."""
    with open(path, "w", encoding="ascii") as obj:
        for j in range(GRID + 1):
            for i in range(GRID + 1):
                obj.write("v %d %d %d\n" % (i, j, (i * 7 + j * 13) % 5))
        for j in range(GRID):
            for i in range(GRID):
                a = j * (GRID + 1) + i + 1
                b, c, d = a + 1, a + GRID + 1, a + GRID + 2
                obj.write("f %d %d %d\nf %d %d %d\n" % (a, b, d, a, d, c))


def main(argv=None):
    parser = argparse.ArgumentParser(description="run the program of a release")
    parser.add_argument("program")
    parser.add_argument("--version", required=True)
    args = parser.parse_args(argv)

    said = subprocess.run([args.program, "--version"], check=True, stdout=subprocess.PIPE,
                          universal_newlines=True).stdout.strip()
    if not said.startswith("bm3d %s " % args.version):
        print("smoke-test.py: --version says %r, expected it to start with 'bm3d %s '" % (said, args.version), file=sys.stderr)
        return 1
    print(said)

    with tempfile.TemporaryDirectory() as directory:
        source, output = os.path.join(directory, "grid.obj"), os.path.join(directory, "grid.bm3d")
        write_grid(source)
        subprocess.run([args.program, "--threads", "2", source, output], check=True)
        with open(output, "rb") as handle:
            magic = handle.read(4)
        if magic != b"BM3D":
            print("smoke-test.py: the output starts with %r, not b'BM3D'" % magic, file=sys.stderr)
            return 1
        print("built %d bytes" % os.path.getsize(output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
