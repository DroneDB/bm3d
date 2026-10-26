#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the fixtures of the viewer's tests, web/tests/fixtures/, with the command line.

They are two of the golden files (tests/golden/): the textured terrain and the sphere with vertex
colours. The golden test program builds them with the bm3d program next to it and writes them,
with their content hash in <name>.sha256, to BM3D_GOLDEN_DIR; this script points that at the
fixtures folder and runs only those two tests. The files are the same bytes on every platform
(the builder is deterministic and the inputs are made with integer arithmetic and square roots),
so running this again with the same program gives the committed files; only the "generator" of
their metadata names the version of the program.

  web-fixtures.py --build <build folder>     the folder where CMake built the tests
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "web", "tests", "fixtures")
# the golden tests whose files are the fixtures, and the names they write
TESTS = {"aTexturedTerrainFromAnObj": "terrain", "aSphereWithColoursFromAPly": "sphere"}


def golden_program(build):
    for name in ("bm3d_golden_tests", "bm3d_golden_tests.exe"):
        for folder in (os.path.join(build, "tests"), os.path.join(build, "tests", "Release")):
            path = os.path.join(folder, name)
            if os.path.isfile(path):
                return path
    raise SystemExit("web-fixtures.py: no bm3d_golden_tests under %s/tests (build the target bm3d_golden_tests)" % build)


def main(argv=None):
    parser = argparse.ArgumentParser(description="write web/tests/fixtures/ with the command line")
    parser.add_argument("--build", required=True, help="the CMake build folder of the tests")
    args = parser.parse_args(argv)
    program = golden_program(args.build)
    env = dict(os.environ, BM3D_GOLDEN_DIR=FIXTURES)
    selected = ":".join("GoldenFixtures." + test for test in TESTS)
    result = subprocess.run([program, "--gtest_filter=" + selected], env=env)
    if result.returncode != 0:
        return result.returncode
    for name in TESTS.values():
        for extension in (".bm3d", ".sha256"):
            if not os.path.isfile(os.path.join(FIXTURES, name + extension)):
                print("web-fixtures.py: %s%s was not written" % (name, extension), file=sys.stderr)
                return 1
    print("wrote %s" % ", ".join(name + ".bm3d" for name in TESTS.values()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
