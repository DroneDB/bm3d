# Changelog

All notable changes to bm3d are listed here. The version is in `VERSION`.

## Unreleased

- Removed the code that exported a mesh as PLY or STL for debugging (`Mesh::savePly`,
  `TMesh::savePly`, `TMesh::savePlyTex`, and `Extractor::savePly`, `saveStl`, `saveUnifiedPly`
  with the code only they used) and the VCGLib export header that came with it. Output does not
  change.
- Removed MECO, the compression Nexus used before corto: the encoder and decoder
  (`src/nxszip`), the flag name and the unused error factor of the extractor. The program has
  always written corto only; flag value 0x2 stays unused in the container header. Output does not
  change.

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
- The same input gives the same file, byte for byte, whatever the run and the number of threads
  (`--threads 1` and `--threads 4` give the same bytes). The node that ends the node table was
  written as it lay in memory, so the same model built twice differed in a few bytes of that node;
  its fields now have fixed values (zero, and the radius -1 of an empty sphere). The file layout and
  everything else it holds are as before. The tests of the `determinism` label check it.
