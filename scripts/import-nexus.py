#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record where the Nexus sources under src/ came from, and which of them have changed since.

The files were copied byte for byte from the DroneDB fork of Nexus (src/ of the pinned
commit) into src/common, src/nxsbuild, src/nxsedit and src/nxszip. Unlike the other imports
they are ours to edit and to move afterwards, so src/NEXUS_SOURCE keeps the SHA-256 of every file
as imported, under the path it has upstream, and a "moved" section with the files that live
elsewhere now. --check reports the files that have been moved, changed or gone, instead of failing.

  import-nexus.py --source <nexus clone> --files <list>   copy the listed files (relative to
                                                          the clone's src/) to where they live
                                                          now and write NEXUS_SOURCE
  import-nexus.py --move OLD NEW [--move OLD NEW ...]     record that a file has moved (paths
                                                          relative to src/; OLD is the upstream
                                                          path or the path it had before)
  import-nexus.py --check                                 list moved, modified and removed files
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
          "SHA-256 of every Nexus file as imported, under its upstream path (relative to the\n"
          "upstream src/). 'moved' maps the upstream path of a file to where it lives in src/\n"
          "now. Files change after the import; scripts/import-nexus.py --check lists the ones\n"
          "that were moved or changed.")


def write_manifest(manifest):
    manifest.write(MANIFEST, BANNER)


def existing_moves():
    return syncutil.Manifest.read(MANIFEST).moves if os.path.isfile(MANIFEST) else {}


def import_files(source, commit, names):
    for name in names:
        if any(word in name for word in NEVER_IMPORT):
            raise syncutil.SyncError("%s must not be imported" % name)
    full = syncutil.resolve_commit(source, commit)
    moves = {name: new for name, new in existing_moves().items() if name in names}
    files = {}
    for name in names:
        data = syncutil.show(source, full, "%s/%s" % (UPSTREAM_PATH, name))
        syncutil.write_file(DEST, moves.get(name, name), data)
        files[name] = syncutil.sha256(data)
    headers = [("repository", REPOSITORY), ("commit", full), ("upstream", UPSTREAM),
               ("upstream-base", UPSTREAM_BASE), ("upstream-path", UPSTREAM_PATH)]
    write_manifest(syncutil.Manifest(headers, [], files, moves))
    print("nexus: %d files from %s" % (len(files), full[:12]))


def record_moves(pairs):
    """Record that files moved. OLD is the upstream path of a file or the path it had before."""
    manifest = syncutil.Manifest.read(MANIFEST)
    for old, new in pairs:
        originals = [name for name in manifest.files if manifest.current_path(name) == old]
        if not originals:
            raise syncutil.SyncError("%s is not a file of the manifest (nor the current path of one)" % old)
        if not os.path.isfile(os.path.join(DEST, *new.split("/"))):
            raise syncutil.SyncError("%s does not exist" % new)
        original = originals[0]
        if new == original:
            manifest.moves.pop(original, None)
        else:
            manifest.moves[original] = new
    write_manifest(manifest)
    print("nexus: %d files moved" % len(manifest.moves))


def check(source, strict):
    manifest = syncutil.Manifest.read(MANIFEST)
    problems = []
    modified, removed, renamed = [], [], []
    for name, digest in sorted(manifest.files.items()):
        current = manifest.current_path(name)
        if not os.path.isfile(os.path.join(DEST, *current.split("/"))):
            removed.append(name)
            continue
        if current != name:
            renamed.append((name, current))
        if syncutil.sha256(syncutil.read_file(DEST, current)) != digest:
            modified.append(current)
    if source:
        commit = syncutil.resolve_commit(source, manifest.header("commit"))
        prefix = manifest.header("upstream-path")
        for name, digest in sorted(manifest.files.items()):
            if syncutil.sha256(syncutil.show(source, commit, "%s/%s" % (prefix, name))) != digest:
                problems.append("recorded hash differs from the pinned commit: " + name)
    print("nexus: %d files imported, %d unchanged, %d modified, %d removed, %d renamed"
          % (len(manifest.files), len(manifest.files) - len(modified) - len(removed),
             len(modified), len(removed), len(renamed)))
    for old, new in renamed:
        print("  renamed:  %s -> %s" % (old, new))
    for name in modified:
        print("  modified: " + name)
    for name in removed:
        print("  removed:  " + name)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems or (strict and (modified or removed or renamed)):
        return 1
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--strict", action="store_true", help="with --check: fail on any change")
    parser.add_argument("--move", nargs=2, action="append", metavar=("OLD", "NEW"),
                        help="record that a file moved (paths relative to src/); repeatable")
    parser.add_argument("--source", metavar="CLONE", help="local clone of the Nexus fork")
    parser.add_argument("--commit", default=DEFAULT_COMMIT)
    parser.add_argument("--files", metavar="LIST",
                        help="file with the paths to import, relative to the clone's src/")
    args = parser.parse_args(argv)
    try:
        if args.check:
            return check(args.source, args.strict)
        if args.move:
            record_moves(args.move)
            return 0
        if not args.source or not args.files:
            parser.error("importing needs --source <clone> and --files <list>")
        import_files(args.source, args.commit, syncutil.read_list(args.files))
        return 0
    except (syncutil.SyncError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
