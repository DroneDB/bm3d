# Changelog

All notable changes to bm3d are listed here. The version is in `VERSION`.

## 0.1.0

First version. It is the multiresolution mesh builder of Nexus, imported from the DroneDB fork
of Nexus, with the fork's hardened loaders and its full-resolution WebP node textures, behind
the bm3d command line and library interface.

- Input: OBJ, PLY and STL.
- Output: the Nexus container, always corto-compressed. This is "format 0", and `bm3d --version`
  reports it as `bm3d 0.1.0 (format 0)`. Files of format 0 are readable by the Nexus tools; the
  `.bm3d` format starts with the first release that writes it.
- `--metadata <file.json>` is accepted and checked to be a JSON object, but the Nexus container
  has no place for it, so it is not written yet.
- VCGLib and corto are copied byte for byte from their upstream repositories at pinned commits
  (`scripts/sync-vcg.py`, `scripts/sync-corto.py`); the Nexus files are recorded with their
  hashes (`scripts/import-nexus.py`). See `PROVENANCE.md`.
