#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record where the Nexus sources under src/ came from, and which of them have changed since.

The files were copied byte for byte from the DroneDB fork of Nexus (src/ of the pinned
commit) into src/common, src/nxsbuild, src/nxsedit and src/nxszip. Unlike the other imports
they are ours to edit afterwards, so src/NEXUS_SOURCE keeps the SHA-256 of every file as
imported and --check reports the files that have changed or gone, instead of failing.

  import-nexus.py --source <nexus clone> --files <list>   copy the listed files (relative to
                                                          the clone's src/) and write NEXUS_SOURCE
  import-nexus.py --check                                 list modified and removed files
  import-nexus.py --check --source <nexus clone>          also verify the recorded hashes
                                                          against the pinned commit
  import-nexus.py --check --strict                        exit 1 if anything differs
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import syncutil  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "src")
MANIFEST = os.path.join(DEST, "NEXUS_SOURCE")
UPSTREAM_PATH = "src"   # folder of the Nexus repository the files come from

REPOSITORY = "https://github.com/DroneDB/nexus"
UPSTREAM = "https://github.com/cnr-isti-vclab/nexus"
UPSTREAM_BASE = "f29e8fd1182b2a1c0c512add99c7edfc8e146680"
DEFAULT_COMMIT = "847f024bf7cc0beb078868749bc7669748c0aa3d"

# Files that must never enter this repository: they have no licence and no known author.
NEVER_IMPORT = ("vertex_cache_optimizer",)

BANNER = ("Written by scripts/import-nexus.py; do not edit.\n"
          "SHA-256 of every Nexus file as imported (paths relative to src/). Files change after\n"
          "the import; scripts/import-nexus.py --check lists the ones that did.")


def import_files(source, commit, names):
    for name in names:
        if any(word in name for word in NEVER_IMPORT):
            raise syncutil.SyncError("%s must not be imported" % name)
    full = syncutil.resolve_commit(source, commit)
    files = {}
    for name in names:
        data = syncutil.show(source, full, "%s/%s" % (UPSTREAM_PATH, name))
        syncutil.write_file(DEST, name, data)
        files[name] = syncutil.sha256(data)
    headers = [("repository", REPOSITORY), ("commit", full), ("upstream", UPSTREAM),
               ("upstream-base", UPSTREAM_BASE), ("upstream-path", UPSTREAM_PATH)]
    syncutil.Manifest(headers, [], files).write(MANIFEST, BANNER)
    print("nexus: %d files from %s" % (len(files), full[:12]))


def check(source, strict):
    manifest = syncutil.Manifest.read(MANIFEST)
    problems = []
    modified, removed = [], []
    for name, digest in sorted(manifest.files.items()):
        path = os.path.join(DEST, *name.split("/"))
        if not os.path.isfile(path):
            removed.append(name)
        elif syncutil.sha256(syncutil.read_file(DEST, name)) != digest:
            modified.append(name)
    if source:
        commit = syncutil.resolve_commit(source, manifest.header("commit"))
        prefix = manifest.header("upstream-path")
        for name, digest in sorted(manifest.files.items()):
            if syncutil.sha256(syncutil.show(source, commit, "%s/%s" % (prefix, name))) != digest:
                problems.append("recorded hash differs from the pinned commit: " + name)
    print("nexus: %d files imported, %d unchanged, %d modified, %d removed"
          % (len(manifest.files), len(manifest.files) - len(modified) - len(removed),
             len(modified), len(removed)))
    for name in modified:
        print("  modified: " + name)
    for name in removed:
        print("  removed:  " + name)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems or (strict and (modified or removed)):
        return 1
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--strict", action="store_true", help="with --check: fail on any change")
    parser.add_argument("--source", metavar="CLONE", help="local clone of the Nexus fork")
    parser.add_argument("--commit", default=DEFAULT_COMMIT)
    parser.add_argument("--files", metavar="LIST",
                        help="file with the paths to import, relative to the clone's src/")
    args = parser.parse_args(argv)
    try:
        if args.check:
            return check(args.source, args.strict)
        if not args.source or not args.files:
            parser.error("importing needs --source <clone> and --files <list>")
        import_files(args.source, args.commit, syncutil.read_list(args.files))
        return 0
    except (syncutil.SyncError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
