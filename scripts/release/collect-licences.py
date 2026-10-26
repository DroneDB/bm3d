#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy the licence texts of the vcpkg ports a build links into a folder.

vcpkg writes the licence of each port it installs to <installed>/<triplet>/share/<port>/copyright
and lists the ports in <installed>/vcpkg/info/<port>_<version>_<triplet>.list. Every port listed
is copied to <out>/<port>-<version>.copyright, except the ones whose name starts with "vcpkg-":
those are the CMake helper ports, files for the build that no program links. A port that has no
copyright file is an error, as an archive must not ship a library without its licence. Prints one
line per port.

  collect-licences.py --installed <build>/vcpkg_installed --triplet x64-linux --out <folder>
"""
import argparse
import os
import re
import shutil
import sys

HELPER_PREFIX = "vcpkg-"


def installed_ports(installed, triplet):
    """[(port, version)] of the ports vcpkg installed for the triplet, by name."""
    info = os.path.join(installed, "vcpkg", "info")
    if not os.path.isdir(info):
        raise SystemExit("collect-licences.py: %s does not exist; is %s a vcpkg_installed folder?" % (info, installed))
    pattern = re.compile(r"^([a-z0-9-]+)_(.+)_%s\.list$" % re.escape(triplet))
    ports = []
    for name in os.listdir(info):
        match = pattern.match(name)
        if match and not match.group(1).startswith(HELPER_PREFIX):
            ports.append((match.group(1), match.group(2)))
    if not ports:
        raise SystemExit("collect-licences.py: no port of %s is listed in %s" % (triplet, info))
    return sorted(ports)


def collect(installed, triplet, out):
    """Copy the licences; returns the names of the files written."""
    os.makedirs(out, exist_ok=True)
    written = []
    for port, version in installed_ports(installed, triplet):
        source = os.path.join(installed, triplet, "share", port, "copyright")
        if not os.path.isfile(source):
            raise SystemExit("collect-licences.py: the port %s has no %s" % (port, source))
        name = "%s-%s.copyright" % (port, version)
        shutil.copyfile(source, os.path.join(out, name))
        written.append(name)
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description="copy the licences of the linked vcpkg ports")
    parser.add_argument("--installed", required=True, help="the vcpkg_installed folder of the build")
    parser.add_argument("--triplet", required=True)
    parser.add_argument("--out", required=True, help="the folder to write the licences to")
    args = parser.parse_args(argv)
    for name in collect(args.installed, args.triplet, args.out):
        print(name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
