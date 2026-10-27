// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads a .bm3d file, version 1 (docs/FORMAT.md). Opening a file runs every check of the header
// and the index (section 11), in the order of the specification, and stops at the first that
// fails with a FormatError; a file that has been opened has passed all of them. The geometry of
// a node and the pixels of a texture are read and checked when they are asked for.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bm3d_format.h"
#include "format_error.h"

namespace bm3d::detail {

// Where the bytes of a file come from: a file on the disk, or a buffer in memory
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual uint64_t size() const = 0;
    // Reads exactly `length` bytes at `offset`, which must lie inside the source
    virtual void read(uint64_t offset, void* where, uint64_t length) = 0;
};

// What corto decodes a node to (section 3.2): the arrays a layout has, in separate vectors
struct DecodedNode {
    std::vector<float> positions;     // 3 x vertexCount
    std::vector<float> uvs;           // 2 x vertexCount, when the layout has texture coordinates
    std::vector<int16_t> normals;     // 3 x vertexCount, when it has normals
    std::vector<uint8_t> colors;      // 4 x vertexCount (RGBA), when it has colours
    std::vector<uint16_t> indices;    // 3 x faceCount
    std::vector<uint32_t> groupEnds;  // the end of each group of the stream, which are the ends of the patches (section 5)
};

class Bm3dReader {
public:
    // Opens a file (UTF-8 path). FormatError for a file that is not a version 1 file; Error
    // (FormatErrorKind::Io) for one that cannot be read
    static Bm3dReader open(const std::string& path);
    // The same for a file held in memory
    static Bm3dReader fromBytes(std::vector<uint8_t> bytes);

    Bm3dReader(Bm3dReader&&) noexcept;
    Bm3dReader& operator=(Bm3dReader&&) noexcept;
    ~Bm3dReader();

    const HeaderRecord& header() const { return header_; }
    const VertexLayout& layout() const { return layout_; }
    uint64_t fileSize() const;

    // The nodes of the index, the sink last (so nodeCount() of them); the patches; the textures
    const std::vector<NodeRecord>& nodes() const { return nodes_; }
    const std::vector<PatchRecord>& patches() const { return patches_; }
    const std::vector<TextureRecord>& textures() const { return textures_; }

    uint32_t nodeCount() const { return header_.nodeCount; }
    uint32_t sink() const { return header_.nodeCount - 1; }
    uint32_t rootCount() const { return rootCount_; }
    // The patches of a real node: firstPatch(node) up to, not including, firstPatch(node + 1)
    uint32_t patchBegin(uint32_t node) const { return nodes_[node].firstPatch; }
    uint32_t patchEnd(uint32_t node) const { return nodes_[node + 1].firstPatch; }
    // The patches of a leaf name the sink
    bool isLeaf(uint32_t node) const { return patches_[patchBegin(node)].node == sink(); }
    // The texture of a real node, -1 for none (a node's patches all have the same one)
    int64_t nodeTexture(uint32_t node) const {
        const uint32_t t = patches_[patchBegin(node)].texture;
        return t == kNoTexture ? -1 : int64_t(t);
    }

    // The bytes of the corto stream of a real node, in a buffer aligned for the decoder
    std::vector<uint8_t> readNodePayload(uint32_t node);
    // Decodes a real node. FormatError (CorruptNode) when corto refuses the stream or its counts
    // differ from the record's
    DecodedNode decodeNode(uint32_t node);
    // The bytes of a texture, checked to be a RIFF/WEBP container; FormatError (CorruptTexture)
    // when they are not
    std::vector<uint8_t> readTexture(uint32_t texture);

    // The metadata block, a JSON object (the empty object for no block). FormatError
    // (BadMetadata) when the block is larger than 1 MiB, or is not valid UTF-8 JSON, or not an object
    std::string metadata();

    // The SHA-256 of the file but for the metadata block, as section 9 defines it, in 64
    // lower-case hexadecimal digits
    std::string contentHash();

private:
    Bm3dReader(std::unique_ptr<ByteSource> source);
    void openChecks();
    void checkIndexRanges();
    void checkIndex();

    std::unique_ptr<ByteSource> source_;
    HeaderRecord header_;
    VertexLayout layout_;
    std::vector<NodeRecord> nodes_;
    std::vector<PatchRecord> patches_;
    std::vector<TextureRecord> textures_;
    uint32_t rootCount_ = 0;
};

}  // namespace bm3d::detail
