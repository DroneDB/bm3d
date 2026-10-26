# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared code of the scripts that import upstream source trees byte for byte.

A component directory holds the imported files, a list of them (FILES) and a manifest
(<NAME>_SOURCE) with the upstream repository, the pinned commit, the patches applied on top
and the SHA-256 of every final file. The upstream bytes always come from git objects
(`git show <commit>:<path>`), never from a working tree, so a checkout with line-ending
conversion cannot change them; the repository marks the imported directories `-text` for the
same reason.

Manifest format (UTF-8, LF):

    # comment
    key: value                    header lines; "patch" lines read "patch: <sha256>  <name>"
    files:
    <sha256>  <relative path>     one line per file, sorted
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

LIST_NAME = "FILES"


class SyncError(Exception):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git(repo, *args, check=True):
    result = subprocess.run(["git", "-C", repo, *args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if check and result.returncode != 0:
        raise SyncError("git %s failed in %s: %s"
                        % (" ".join(args), repo, result.stderr.decode(errors="replace").strip()))
    return result


def resolve_commit(repo, commit):
    """Return the full hash of a commit that exists in a local clone."""
    return git(repo, "rev-parse", "--verify", commit + "^{commit}").stdout.decode().strip()


def show(repo, commit, path):
    """Bytes of a file as stored in a commit."""
    return git(repo, "show", "%s:%s" % (commit, path)).stdout


def read_list(path):
    with open(path, encoding="utf-8", newline="") as handle:
        lines = handle.read().split("\n")
    names = [line for line in lines if line and not line.startswith("#")]
    if names != sorted(set(names)):
        raise SyncError("%s must be sorted without duplicates" % path)
    return names


def write_list(path, names):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("".join(name + "\n" for name in sorted(set(names))))


class Manifest:
    def __init__(self, headers, patches, files):
        self.headers = headers    # ordered list of (key, value), without patch lines
        self.patches = patches    # list of (name, sha256)
        self.files = files        # dict path -> sha256

    def header(self, key):
        for k, v in self.headers:
            if k == key:
                return v
        raise SyncError("manifest has no %r line" % key)

    @classmethod
    def read(cls, path):
        headers, patches, files = [], [], {}
        in_files = False
        with open(path, encoding="utf-8", newline="") as handle:
            for raw in handle.read().split("\n"):
                line = raw.rstrip("\r")
                if not line or line.startswith("#"):
                    continue
                if in_files:
                    digest, sep, name = line.partition("  ")
                    if not sep or len(digest) != 64:
                        raise SyncError("%s: bad file line %r" % (path, line))
                    files[name] = digest
                elif line == "files:":
                    in_files = True
                else:
                    key, sep, value = line.partition(": ")
                    if not sep:
                        raise SyncError("%s: bad line %r" % (path, line))
                    if key == "patch":
                        digest, sep2, name = value.partition("  ")
                        if not sep2:
                            raise SyncError("%s: bad patch line %r" % (path, line))
                        patches.append((name, digest))
                    else:
                        headers.append((key, value))
        return cls(headers, patches, files)

    def write(self, path, banner):
        out = ["# " + line if line else "#" for line in banner.split("\n")]
        out += ["%s: %s" % (k, v) for k, v in self.headers]
        out += ["patch: %s  %s" % (digest, name) for name, digest in self.patches]
        out.append("files:")
        out += ["%s  %s" % (self.files[name], name) for name in sorted(self.files)]
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(out) + "\n")


def disk_files(root, ignore):
    """All files below root as sorted forward-slash paths, leaving out the bookkeeping files."""
    found = []
    for base, _, names in os.walk(root):
        for name in names:
            rel = os.path.relpath(os.path.join(base, name), root).replace(os.sep, "/")
            if rel not in ignore:
                found.append(rel)
    return sorted(found)


def read_file(root, rel):
    with open(os.path.join(root, *rel.split("/")), "rb") as handle:
        return handle.read()


def write_file(root, rel, data):
    path = os.path.join(root, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


def patch_list(patch_dir):
    if not os.path.isdir(patch_dir):
        return []
    return sorted(n for n in os.listdir(patch_dir) if n.endswith(".patch"))


def apply_patches(tree, patch_dir, names):
    """Apply the patches to a directory with `git apply`, which needs no repository for that."""
    for name in names:
        result = subprocess.run(
            ["git", "apply", "--whitespace=nowarn", os.path.join(os.path.abspath(patch_dir), name)],
            cwd=tree, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode != 0:
            raise SyncError("patch %s does not apply: %s"
                            % (name, result.stderr.decode(errors="replace").strip()))


def upstream_files(source, commit, names, patch_dir=None, patches=()):
    """Return {path: bytes} of the listed files at a commit, patches applied on top."""
    contents = {name: show(source, commit, name) for name in names}
    if not patches:
        return contents
    scratch = tempfile.mkdtemp(prefix="bm3d-sync-")
    try:
        for name, data in contents.items():
            write_file(scratch, name, data)
        apply_patches(scratch, patch_dir, patches)
        return {name: read_file(scratch, name) for name in names}
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


class Component:
    """One imported upstream tree: where it lives and how it is described."""

    def __init__(self, name, dest, manifest_name, repository, default_commit, patch_dir=None):
        self.name = name
        self.dest = dest                      # directory holding the files, FILES and manifest
        self.manifest_name = manifest_name
        self.repository = repository
        self.default_commit = default_commit
        self.patch_dir = patch_dir
        self.list_path = os.path.join(dest, LIST_NAME)
        self.manifest_path = os.path.join(dest, manifest_name)

    def bookkeeping(self):
        return {LIST_NAME, self.manifest_name}

    def banner(self, script):
        return ("Written by scripts/%s; do not edit.\n"
                "The files below are the upstream bytes of the commit, with the patches applied.\n"
                "Check: scripts/%s --check   (add --source <clone> to re-derive them)"
                % (script, script))


def sync(component, source, commit, script):
    names = read_list(component.list_path)
    full = resolve_commit(source, commit)
    patches = patch_list(component.patch_dir) if component.patch_dir else []
    contents = upstream_files(source, full, names, component.patch_dir, patches)
    stale = set(disk_files(component.dest, component.bookkeeping())) - set(names)
    for rel in sorted(stale):
        os.remove(os.path.join(component.dest, *rel.split("/")))
    for name, data in contents.items():
        write_file(component.dest, name, data)
    headers = [("repository", component.repository), ("commit", full)]
    patch_entries = [(n, sha256(read_file(component.patch_dir, n))) for n in patches]
    manifest = Manifest(headers, patch_entries, {n: sha256(d) for n, d in contents.items()})
    manifest.write(component.manifest_path, component.banner(script))
    print("%s: %d files from %s%s" % (component.name, len(names), full[:12],
                                      (", %d patches" % len(patches)) if patches else ""))


def check_manifest_files(component, manifest, problems):
    """Committed files == FILES == manifest, and every hash matches."""
    names = read_list(component.list_path)
    on_disk = disk_files(component.dest, component.bookkeeping())
    for rel in sorted(set(on_disk) - set(names)):
        problems.append("%s: not in %s: %s" % (component.name, LIST_NAME, rel))
    for rel in sorted(set(names) - set(on_disk)):
        problems.append("%s: listed but missing: %s" % (component.name, rel))
    for rel in sorted(set(names) ^ set(manifest.files)):
        problems.append("%s: %s and %s disagree on %s"
                        % (component.name, LIST_NAME, component.manifest_name, rel))
    for rel in sorted(set(names) & set(on_disk) & set(manifest.files)):
        if sha256(read_file(component.dest, rel)) != manifest.files[rel]:
            problems.append("%s: hash differs: %s" % (component.name, rel))
    if component.patch_dir:
        listed = [n for n, _ in manifest.patches]
        if listed != patch_list(component.patch_dir):
            problems.append("%s: patch files %s differ from the manifest %s"
                            % (component.name, patch_list(component.patch_dir), listed))
        for name, digest in manifest.patches:
            path = os.path.join(component.patch_dir, name)
            if not os.path.isfile(path) or sha256(read_file(component.patch_dir, name)) != digest:
                problems.append("%s: patch changed or missing: %s" % (component.name, name))
    return names


def check(component, source=None):
    problems = []
    manifest = Manifest.read(component.manifest_path)
    names = check_manifest_files(component, manifest, problems)
    if source is not None and not problems:
        commit = resolve_commit(source, manifest.header("commit"))
        patches = [n for n, _ in manifest.patches]
        derived = upstream_files(source, commit, names, component.patch_dir, patches)
        for rel in names:
            if derived[rel] != read_file(component.dest, rel):
                problems.append("%s: differs from upstream %s plus patches: %s"
                                % (component.name, commit[:12], rel))
    return problems


def main(component, script, argv=None):
    parser = argparse.ArgumentParser(
        description="Import or verify %s byte for byte from upstream." % component.name)
    parser.add_argument("--check", action="store_true",
                        help="verify the committed files against %s and the manifest (no network)"
                        % LIST_NAME)
    parser.add_argument("--source", metavar="CLONE",
                        help="local clone of %s: with --check also re-derive the files from it; "
                        "without, import from it" % component.repository)
    parser.add_argument("--commit", default=component.default_commit,
                        help="commit to import (default: %(default)s)")
    args = parser.parse_args(argv)
    try:
        if args.check:
            problems = check(component, args.source)
            for problem in problems:
                print(problem, file=sys.stderr)
            if problems:
                return 1
            print("%s: ok%s" % (component.name, " (re-derived from upstream)" if args.source else ""))
            return 0
        if not args.source:
            parser.error("importing needs --source <local clone>")
        sync(component, args.source, args.commit, script)
        return 0
    except (SyncError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
