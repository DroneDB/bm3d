// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes a .bm3d file, version 1 (docs/FORMAT.md, sections 2 and 12). The writer lays the file
// out and computes nothing the specification takes from elsewhere: the corto streams, the
// counts the encoder left in them, the spheres, the errors and the cones of the nodes arrive
// from the builder, and the writer puts them where the format says.
//
// The index comes first in the file and the counts it needs are known before the first payload
// is written: the caller says how many nodes, patches and textures there will be, and adds the
// nodes in node order, then the textures in texture order. finish() writes the metadata, goes
// back to write the header and the index, and gives the file its name; until then the file is
// written under another name next to it, and a writer that is destroyed unfinished removes it.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../common/textfile.h"
#include "bm3d_format.h"

namespace bm3d::detail {

// Throws MetadataTooLargeError when a metadata block of `length` bytes is longer than a file takes
void checkMetadataLength(uint64_t length);

class Bm3dWriter {
public:
    struct Counts {
        uint32_t nodes = 0;     // the real nodes, without the sink
        uint32_t patches = 0;   // all of them
        uint32_t textures = 0;
    };

    // Creates `path` + ".part" and reserves the room of the header and the index. Throws Error
    // when the file cannot be created or when the layout and the counts disagree (texture
    // coordinates if and only if there are textures).
    Bm3dWriter(const std::string& path, const VertexLayout& layout, const Counts& counts);
    ~Bm3dWriter();

    Bm3dWriter(const Bm3dWriter&) = delete;
    Bm3dWriter& operator=(const Bm3dWriter&) = delete;

    // The next node, with its patches and the corto stream of its geometry. From `node` the
    // writer takes the error, the cone, the spheres and the counts, which must be those of the
    // encoder after encode() (the patches' triangleOffset too); it sets the payload offset and
    // length and the first patch. A node whose stream holds no triangle is an Error: the file is
    // not written. The `node` of a patch is the index of the child in the file, the sink being
    // Counts::nodes.
    void addNode(const NodeRecord& node, const PatchRecord* patches, size_t patchCount, const void* payload,
                 uint64_t length);

    // The next texture, exactly the bytes of the WebP image
    void addTexture(const void* image, uint64_t length);

    // Writes the metadata (a JSON object or an empty string; MetadataTooLargeError above 1 MiB),
    // the header and the index, and renames the file. Every node and texture must have been added.
    void finish(const SphereRecord& sphere, const std::string& metadata);

private:
    // Appends zero bytes up to the next multiple of 8
    void padToAlignment();
    void discard();

    std::string path_;
    std::string partPath_;
    VertexLayout layout_;
    Counts counts_;
    IndexLayout index_;
    OutputFile file_;
    bool created_ = false;
    bool finished_ = false;

    std::vector<NodeRecord> nodes_;
    std::vector<PatchRecord> patches_;
    std::vector<TextureRecord> textures_;
    uint64_t vertexCount_ = 0;
    uint64_t faceCount_ = 0;
};

}  // namespace bm3d::detail
