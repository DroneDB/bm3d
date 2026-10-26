#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the archive of the program for one platform: bm3d-<version>-<platform>.tar.gz for Linux,
.zip for Windows, the same layout in both:

  bm3d-<version>-<platform>/
    bm3d (bm3d.exe)  LICENSE  NOTICE  PROVENANCE.md  README.md
    licenses/                 the texts of the licences of Nexus, VCGLib and corto (licenses/ of
                              the repository)
    licenses/ports/           the licence of each vcpkg port the program links
                              (collect-licences.py)

The files of the archive have owner root, mode 755 for the program and 644 for the rest, and the
time SOURCE_DATE_EPOCH names (the commit time, in the release workflow; otherwise the time of
the run), so that the same inputs give the same bytes.

  package.py --version 1.0.0 --platform linux-x64 --binary <bm3d> \\
             --installed <build>/vcpkg_installed --triplet x64-linux --out <folder>
"""
import argparse
import gzip
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOP_LEVEL_FILES = ("LICENSE", "NOTICE", "PROVENANCE.md", "README.md")


def timestamp():
    return int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))


def stage(directory, binary, installed, triplet):
    """Fill `directory` with the files of the archive; returns their paths relative to it."""
    program = "bm3d.exe" if binary.endswith(".exe") else "bm3d"
    shutil.copyfile(binary, os.path.join(directory, program))
    for name in TOP_LEVEL_FILES:
        shutil.copyfile(os.path.join(ROOT, name), os.path.join(directory, name))
    shutil.copytree(os.path.join(ROOT, "licenses"), os.path.join(directory, "licenses"))
    subprocess.run([sys.executable, os.path.join(HERE, "collect-licences.py"), "--installed", installed,
                    "--triplet", triplet, "--out", os.path.join(directory, "licenses", "ports")], check=True)
    files = []
    for base, dirs, names in os.walk(directory):
        dirs.sort()
        files.extend(os.path.relpath(os.path.join(base, n), directory).replace(os.sep, "/") for n in sorted(names))
    return sorted(files), program


def write_tar(path, prefix, directory, files, program, mtime):
    # gzip's own header holds a time; fixing it makes the file reproducible
    with open(path, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=mtime, compresslevel=9) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for name in files:
                info = tarfile.TarInfo("%s/%s" % (prefix, name))
                with open(os.path.join(directory, name), "rb") as handle:
                    data = handle.read()
                info.size, info.mtime = len(data), mtime
                info.mode = 0o755 if name == program else 0o644
                info.uname = info.gname = "root"
                archive.addfile(info, io.BytesIO(data))


def write_zip(path, prefix, directory, files, program, mtime):
    stamp = time.gmtime(max(mtime, 315532800))[:6]  # zip cannot hold a date before 1980
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in files:
            info = zipfile.ZipInfo("%s/%s" % (prefix, name), stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if name == program else 0o644) << 16
            with open(os.path.join(directory, name), "rb") as handle:
                archive.writestr(info, handle.read())


def main(argv=None):
    parser = argparse.ArgumentParser(description="make the release archive of one platform")
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True, choices=("linux-x64", "windows-x64"))
    parser.add_argument("--binary", required=True)
    parser.add_argument("--installed", required=True, help="the vcpkg_installed folder of the build")
    parser.add_argument("--triplet", required=True)
    parser.add_argument("--out", required=True, help="the folder the archive is written to")
    args = parser.parse_args(argv)

    prefix = "bm3d-%s-%s" % (args.version, args.platform)
    os.makedirs(args.out, exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        files, program = stage(directory, args.binary, args.installed, args.triplet)
        if args.platform == "windows-x64":
            path, write = os.path.join(args.out, prefix + ".zip"), write_zip
        else:
            path, write = os.path.join(args.out, prefix + ".tar.gz"), write_tar
        write(path, prefix, directory, files, program, timestamp())
    print(path)
    for name in files:
        print("  %s/%s" % (prefix, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
