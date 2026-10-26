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
- The program reads three formats, OBJ, PLY and STL, and says so. The extension is read in any
  case (`MODEL.OBJ` is an OBJ). A file with another extension (`.ts`, `.tsp`, `.off`, none) is
  invalid input, exit code 2, with a message that names `obj`, `ply` and `stl`; it used to be tried
  with a VCGLib importer. The loaders for Nexus' `.ts` and `.tsp` files, its colormap and the
  VCGLib importer loader are removed, and with them `TMesh::loadPly`. Output does not change.
- Removed the code paths the program never takes, and the options that nothing set:
  - point clouds: the cloud stream and kd-tree, the loaders' vertex reading, the cloud levels of the
    builder and the cloud simplification of the meshes;
  - deepzoom: nodes and textures in the files of a folder beside the container (the flag value 0x8
    stays unused in the header);
  - node textures that are the original image files, node textures rounded up to a power of two, and
    JPEG node textures (the node textures have been WebP). JPEG stays for reading input textures
    and for the texture tiles the builder spills to disk;
  - the origin, scale and vertex quantization of the loaders, which stayed at no offset, no scale
    and no quantization. The ASCII STL loader multiplied the x coordinate by the third scale
    instead of the z by it, which did nothing at a scale of 1 and is gone with the scale.
  Output does not change.
- `third_party/vcg` holds 96 files, not 123: the ones that only the removed code read are no longer
  copied (VCGLib's importers and exporters, its kd-tree and a few mesh algorithms). The files left
  are the same bytes as before.
- A vertex with a NaN, an infinity or a number beyond the range of a float as a coordinate is
  invalid input (exit code 2) in every format, with the file and the vertex or triangle in the
  message. Only OBJ refused it before; a PLY, a binary STL or an ASCII STL with one made the build
  run for ever. The PLY loader also starts each vertex it reads from zero, not from the stack: the
  alpha of the colour of a PLY that has red, green and blue and no alpha was whatever the stack
  held (0 in the files built so far).
- STL files are read by their size, not by how they start. A file of 84 + 50 n bytes, n being the
  count in its header, is binary, whatever its header says (a binary STL that begins with `solid`,
  which many programs write, was read as ASCII and gave garbage or no model); otherwise a file that
  starts with `solid` and a facet is ASCII; otherwise a longer file is binary and its n triangles
  are read, with a warning for the bytes after them; a shorter one is refused as truncated (exit
  code 2) instead of read as far as it goes. The numbers are read as little-endian floats from the
  bytes, without the unaligned loads UBSan reported. The ASCII reader takes words, not lines: any
  white space, blank lines, Windows line ends, keywords in any case and several solids in a file;
  an error names the line. A binary STL whose header holds `COLOR=` (Materialise Magics, and the
  header MeshLab writes) gives vertex colours when some triangle has a colour of its own: bit 15
  of the two bytes after a triangle set, then red in bits 0-4, green in 5-9 and blue in 10-14; the
  other triangles get the colour of the header. The bit order of VisCAM and SolidView, which is
  the opposite, is not read. The two sample STL files give the same bytes as before.
- A file of random bytes named `.stl` no longer keeps the program busy for ever. The cause was not
  the kd-tree: the search for the bounding sphere of a node (`vcg::Sphere3::CreateTight`) loops until
  its step is smaller than its radius, and with coordinates of the size of the floats (random bytes
  give them) the distances overflow, the radius becomes NaN, and the comparison is never true.
  The same model of 1e12 units across crashed in the cone of the normals instead, where the product
  of two edges overflows. A model whose bounding box is more than 1e9 units across (a city is 1e7
  millimetres) is now refused as invalid input, in every format, with its size in the message.
- PLY: the texture coordinates of a vertex are read from `u`, `v` or from `texture_u`, `texture_v`
  as well as from `s`, `t` (the first pair the file has). A colour without an `alpha` property is
  opaque, 255; the alpha was whatever the stack held (0 in the files built so far), so the
  vertices of `brighton.ply` have an alpha of 255 now, and nothing else of its output changes.
  The loader no longer takes the fractional part of the texture coordinates (`modf`): the builder
  does it for the coordinates of every format when it assigns the vertices to textures, so a
  coordinate beyond 1 gives the same model as before, and the PLY loader no longer rewrites the
  coordinate it keeps for all the faces that share a vertex.
- OBJ: `v x y z r g b` (exactly three numbers after the position) is the colour of the vertex,
  from 0 to 1 unless a component of that vertex is more than 1, then from 0 to 255; `v x y z w`
  (a weight) and any other count of numbers is not a colour. The colour of a vertex wins over the
  colour of the material (`Kd`) of the faces that use it; a vertex without one gets the colour of
  the material, or grey when the model has colours on some vertices and no material. An OBJ
  with the colours of MeshLab (`brighton_beach.obj` of the test data) used to lose them; its
  output holds them now (its geometry is as before).

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
