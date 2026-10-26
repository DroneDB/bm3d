// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The records of the .bm3d file, version 1 (docs/FORMAT.md is the specification): the header and
// the node, patch and texture records, their sizes, and the functions that turn each into its
// bytes and back. Every field is written and read one by one as a little-endian number, so the
// file does not depend on the layout of a struct in memory or on the byte order of the machine;
// the sizes the structs have are asserted only because the tables are arrays of them.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bm3d::detail {

// --- constants of the specification ----------------------------------------------------------

constexpr uint32_t kFormatVersion = 1;       // the one version this code reads and writes
constexpr uint8_t kMagic[4] = {0x42, 0x4D, 0x33, 0x44};  // "BM3D"

constexpr uint32_t kHeaderSize = 128;
constexpr uint32_t kNodeRecordSize = 56;
constexpr uint32_t kPatchRecordSize = 12;
constexpr uint32_t kTextureRecordSize = 16;
constexpr uint32_t kAttributeSlotSize = 2;
constexpr uint32_t kAttributeSlots = 8;
constexpr uint64_t kAlignment = 8;

constexpr uint32_t kFlagCortoCompressed = 0x1;
constexpr uint32_t kFlagTextureWebp = 0x2;
constexpr uint32_t kKnownFlags = kFlagCortoCompressed | kFlagTextureWebp;

constexpr uint32_t kNoTexture = 0xFFFFFFFFu;
constexpr uint64_t kMaxMetadataLength = 1048576;  // 1 MiB

// The offset of the field metadataLength in the header, which the content hash reads as zero
constexpr uint64_t kMetadataLengthOffset = 72;

enum class AttributeType : uint8_t {
    None = 0,
    Byte,
    UnsignedByte,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    Float,
    Double,
};

// --- records -------------------------------------------------------------------------------------

struct AttributeSlot {
    uint8_t type = 0;    // an AttributeType
    uint8_t number = 0;  // components
};
static_assert(sizeof(AttributeSlot) == kAttributeSlotSize, "an attribute slot is two bytes");

// A sphere: centre and radius
struct SphereRecord {
    float center[3] = {0, 0, 0};
    float radius = 0;
};
static_assert(sizeof(SphereRecord) == 16, "a sphere is four floats");

struct HeaderRecord {
    uint8_t magic[4] = {kMagic[0], kMagic[1], kMagic[2], kMagic[3]};
    uint32_t formatVersion = kFormatVersion;
    uint32_t headerSize = kHeaderSize;
    uint32_t flags = 0;
    AttributeSlot vertexLayout[kAttributeSlots];
    AttributeSlot faceLayout[kAttributeSlots];
    uint64_t vertexCount = 0;
    uint64_t faceCount = 0;
    uint64_t metadataOffset = 0;
    uint64_t metadataLength = 0;
    uint64_t indexOffset = 0;
    uint64_t indexLength = 0;
    uint32_t nodeCount = 0;  // with the sink
    uint32_t patchCount = 0;
    uint32_t textureCount = 0;
    SphereRecord sphere;
    uint8_t reserved[4] = {0, 0, 0, 0};
};
static_assert(sizeof(HeaderRecord) == kHeaderSize, "the header is 128 bytes");
static_assert(offsetof(HeaderRecord, vertexLayout) == 16 && offsetof(HeaderRecord, faceLayout) == 32 &&
                  offsetof(HeaderRecord, vertexCount) == 48 && offsetof(HeaderRecord, metadataOffset) == 64 &&
                  offsetof(HeaderRecord, metadataLength) == kMetadataLengthOffset &&
                  offsetof(HeaderRecord, indexOffset) == 80 && offsetof(HeaderRecord, nodeCount) == 96 &&
                  offsetof(HeaderRecord, sphere) == 108 && offsetof(HeaderRecord, reserved) == 124,
              "the fields of the header are where the specification puts them");

struct NodeRecord {
    uint64_t payloadOffset = 0;
    uint64_t payloadLength = 0;
    float error = 0;
    uint16_t vertexCount = 0;
    uint16_t faceCount = 0;
    int16_t cone[4] = {0, 0, 0, 0};
    SphereRecord sphere;  // the saturated sphere
    float tightRadius = 0;
    uint32_t firstPatch = 0;
};
static_assert(sizeof(NodeRecord) == kNodeRecordSize, "a node record is 56 bytes");
static_assert(offsetof(NodeRecord, error) == 16 && offsetof(NodeRecord, cone) == 24 &&
                  offsetof(NodeRecord, sphere) == 32 && offsetof(NodeRecord, tightRadius) == 48 &&
                  offsetof(NodeRecord, firstPatch) == 52,
              "the fields of a node record are where the specification puts them");

struct PatchRecord {
    uint32_t node = 0;            // the child; the sink for the leaves
    uint32_t triangleOffset = 0;  // the end of the patch in the triangle list of the node
    uint32_t texture = kNoTexture;
};
static_assert(sizeof(PatchRecord) == kPatchRecordSize, "a patch record is 12 bytes");

struct TextureRecord {
    uint64_t offset = 0;
    uint64_t length = 0;
};
static_assert(sizeof(TextureRecord) == kTextureRecordSize, "a texture record is 16 bytes");

// --- serialisation: one field at a time, little endian ---------------------------------------

// Each function writes or reads exactly the size of its record at `bytes`
void encodeHeaderRecord(const HeaderRecord& header, uint8_t* bytes);
HeaderRecord decodeHeaderRecord(const uint8_t* bytes);
void encodeNodeRecord(const NodeRecord& node, uint8_t* bytes);
NodeRecord decodeNodeRecord(const uint8_t* bytes);
void encodePatchRecord(const PatchRecord& patch, uint8_t* bytes);
PatchRecord decodePatchRecord(const uint8_t* bytes);
void encodeTextureRecord(const TextureRecord& texture, uint8_t* bytes);
TextureRecord decodeTextureRecord(const uint8_t* bytes);

// --- what the layout of a file says ------------------------------------------------------------

// The attributes of a decoded vertex that version 1 allows to vary (positions and the vertex
// indexes of the triangles are always there)
struct VertexLayout {
    bool normals = false;    // SHORT x 3
    bool colors = false;     // UNSIGNED_BYTE x 4
    bool texCoords = false;  // FLOAT x 2

    bool operator==(const VertexLayout& other) const {
        return normals == other.normals && colors == other.colors && texCoords == other.texCoords;
    }
};

// Bytes of a vertex once decoded (positions, texture coordinates, normals, colours) and of a
// triangle
size_t decodedVertexBytes(const VertexLayout& layout);
constexpr size_t kDecodedTriangleBytes = 6;

// The slots of the header for a layout
void layoutToSlots(const VertexLayout& layout, AttributeSlot vertex[kAttributeSlots], AttributeSlot face[kAttributeSlots]);
// The layout the slots of a header describe; false when they are not one of version 1
bool slotsToLayout(const AttributeSlot vertex[kAttributeSlots], const AttributeSlot face[kAttributeSlots], VertexLayout& layout);

// --- the geometry of the file -------------------------------------------------------------------

constexpr uint64_t alignUp(uint64_t value) { return (value + (kAlignment - 1)) & ~(kAlignment - 1); }

// Where the three tables lie inside the index, from the counts of the header (section 4)
struct IndexLayout {
    uint64_t nodeTable = 0;          // from the start of the index
    uint64_t patchTable = 0;
    uint64_t textureTable = 0;       // textureTableStart
    uint64_t length = 0;             // indexLength
};
IndexLayout indexLayout(uint32_t nodeCount, uint32_t patchCount, uint32_t textureCount);

// The number of roots of a model whose nodes are stored parents first with a sink last: the
// nodes before the smallest node any root links to. `nodeCount` includes the sink;
// firstPatch(n) is the first patch of node n (for the sink too: it ends the patches of the
// node before it) and child(p) the node patch p links to. Only real nodes are visited.
template <typename FirstPatch, typename Child>
uint32_t countRoots(uint32_t nodeCount, FirstPatch firstPatch, Child child) {
    if (nodeCount == 0)
        return 0;
    uint32_t roots = nodeCount - 1;  // the sink is never a root
    for (uint32_t j = 0; j < roots; j++) {
        for (uint32_t p = firstPatch(j); p < firstPatch(j + 1); p++)
            if (child(p) < roots)
                roots = child(p);
    }
    return roots;
}

}  // namespace bm3d::detail
