// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The records of the format (src/format/bm3d_format.h): their sizes, the bytes each field becomes
// (little endian, at the offset the specification gives), the geometry of the index and the
// layouts a header may name.

#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "format/bm3d_format.h"

namespace {

using namespace bm3d::detail;

std::vector<uint8_t> bytesOf(const std::vector<int>& values) {
    return std::vector<uint8_t>(values.begin(), values.end());
}

TEST(FormatRecords, theRecordsHaveTheSizesOfTheSpecification) {
    EXPECT_EQ(sizeof(HeaderRecord), 128u);
    EXPECT_EQ(sizeof(NodeRecord), 56u);
    EXPECT_EQ(sizeof(PatchRecord), 12u);
    EXPECT_EQ(sizeof(TextureRecord), 16u);
    EXPECT_EQ(sizeof(AttributeSlot), 2u);
    EXPECT_EQ(kHeaderSize, 128u);
    EXPECT_EQ(kNodeRecordSize, 56u);
    EXPECT_EQ(kPatchRecordSize, 12u);
    EXPECT_EQ(kTextureRecordSize, 16u);
}

// Every field has a value of its own, so that a field written at the wrong place shows
HeaderRecord distinctHeader() {
    HeaderRecord h;
    h.formatVersion = 1;
    h.headerSize = 128;
    h.flags = 3;
    for (uint32_t i = 0; i < kAttributeSlots; i++) {
        h.vertexLayout[i] = {uint8_t(i + 1), uint8_t(i + 11)};
        h.faceLayout[i] = {uint8_t(i + 21), uint8_t(i + 31)};
    }
    h.vertexCount = 0x0102030405060708ull;
    h.faceCount = 0x1112131415161718ull;
    h.metadataOffset = 0x2122232425262728ull;
    h.metadataLength = 0x3132333435363738ull;
    h.indexOffset = 0x4142434445464748ull;
    h.indexLength = 0x5152535455565758ull;
    h.nodeCount = 0x61626364;
    h.patchCount = 0x71727374;
    h.textureCount = 0x81828384;
    h.sphere.center[0] = 1.5f;
    h.sphere.center[1] = -2.25f;
    h.sphere.center[2] = 0.0f;
    h.sphere.radius = 1e10f;
    h.reserved[0] = 0xA1;
    h.reserved[1] = 0xA2;
    h.reserved[2] = 0xA3;
    h.reserved[3] = 0xA4;
    return h;
}

TEST(FormatRecords, aHeaderIsItsFieldsAtTheOffsetsOfTheSpecificationLittleEndian) {
    uint8_t bytes[kHeaderSize];
    std::memset(bytes, 0xEE, sizeof(bytes));
    encodeHeaderRecord(distinctHeader(), bytes);

    EXPECT_EQ(std::vector<uint8_t>(bytes + 0, bytes + 16), bytesOf({0x42, 0x4D, 0x33, 0x44, 1, 0, 0, 0, 128, 0, 0, 0, 3, 0, 0, 0}));
    EXPECT_EQ(std::vector<uint8_t>(bytes + 16, bytes + 32),
              bytesOf({1, 11, 2, 12, 3, 13, 4, 14, 5, 15, 6, 16, 7, 17, 8, 18}));
    EXPECT_EQ(std::vector<uint8_t>(bytes + 32, bytes + 48),
              bytesOf({21, 31, 22, 32, 23, 33, 24, 34, 25, 35, 26, 36, 27, 37, 28, 38}));
    EXPECT_EQ(std::vector<uint8_t>(bytes + 48, bytes + 64),
              bytesOf({0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11}));
    EXPECT_EQ(std::vector<uint8_t>(bytes + 64, bytes + 80),
              bytesOf({0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31}));
    EXPECT_EQ(std::vector<uint8_t>(bytes + 80, bytes + 96),
              bytesOf({0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41, 0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51}));
    EXPECT_EQ(std::vector<uint8_t>(bytes + 96, bytes + 108),
              bytesOf({0x64, 0x63, 0x62, 0x61, 0x74, 0x73, 0x72, 0x71, 0x84, 0x83, 0x82, 0x81}));
    // 1.5, -2.25 and 0 as IEEE 754 binary32, then 1e10 (0x501502F9)
    EXPECT_EQ(std::vector<uint8_t>(bytes + 108, bytes + 128),
              bytesOf({0x00, 0x00, 0xC0, 0x3F, 0x00, 0x00, 0x10, 0xC0, 0x00, 0x00, 0x00, 0x00, 0xF9, 0x02, 0x15, 0x50, 0xA1, 0xA2, 0xA3, 0xA4}));
}

TEST(FormatRecords, aHeaderIsReadBackAsItWasWritten) {
    uint8_t bytes[kHeaderSize];
    const HeaderRecord h = distinctHeader();
    encodeHeaderRecord(h, bytes);
    const HeaderRecord back = decodeHeaderRecord(bytes);
    uint8_t again[kHeaderSize];
    encodeHeaderRecord(back, again);
    EXPECT_EQ(std::memcmp(bytes, again, kHeaderSize), 0);
    EXPECT_EQ(back.vertexCount, h.vertexCount);
    EXPECT_EQ(back.metadataLength, h.metadataLength);
    EXPECT_EQ(back.textureCount, h.textureCount);
    EXPECT_EQ(back.sphere.center[1], -2.25f);
    EXPECT_EQ(back.faceLayout[7].number, 38);
    EXPECT_EQ(back.reserved[3], 0xA4);
}

NodeRecord distinctNode() {
    NodeRecord n;
    n.payloadOffset = 0x0102030405060708ull;
    n.payloadLength = 0x1112131415161718ull;
    n.error = 0.5f;
    n.vertexCount = 0x2122;
    n.faceCount = 0x3132;
    n.cone[0] = 0x0102;
    n.cone[1] = -2;
    n.cone[2] = 32766;
    n.cone[3] = -32768;
    n.sphere.center[0] = 2.0f;
    n.sphere.center[1] = 4.0f;
    n.sphere.center[2] = -8.0f;
    n.sphere.radius = 16.0f;
    n.tightRadius = 1.0f;
    n.firstPatch = 0x41424344;
    return n;
}

TEST(FormatRecords, aNodeIsItsFieldsAtTheOffsetsOfTheSpecificationLittleEndian) {
    uint8_t bytes[kNodeRecordSize];
    std::memset(bytes, 0xEE, sizeof(bytes));
    encodeNodeRecord(distinctNode(), bytes);
    const std::vector<uint8_t> expected = bytesOf({
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,    // 0 payloadOffset
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,    // 8 payloadLength
        0x00, 0x00, 0x00, 0x3F,                            // 16 error 0.5
        0x22, 0x21, 0x32, 0x31,                            // 20 vertexCount, 22 faceCount
        0x02, 0x01, 0xFE, 0xFF, 0xFE, 0x7F, 0x00, 0x80,    // 24 cone
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x80, 0x40,    // 32 sphere.x 2, sphere.y 4
        0x00, 0x00, 0x00, 0xC1, 0x00, 0x00, 0x80, 0x41,    // 40 sphere.z -8, sphere.radius 16
        0x00, 0x00, 0x80, 0x3F,                            // 48 tightRadius 1
        0x44, 0x43, 0x42, 0x41,                            // 52 firstPatch
    });
    EXPECT_EQ(std::vector<uint8_t>(bytes, bytes + kNodeRecordSize), expected);

    const NodeRecord back = decodeNodeRecord(bytes);
    EXPECT_EQ(back.payloadOffset, 0x0102030405060708ull);
    EXPECT_EQ(back.payloadLength, 0x1112131415161718ull);
    EXPECT_EQ(back.error, 0.5f);
    EXPECT_EQ(back.vertexCount, 0x2122);
    EXPECT_EQ(back.faceCount, 0x3132);
    EXPECT_EQ(back.cone[1], -2);
    EXPECT_EQ(back.cone[3], -32768);
    EXPECT_EQ(back.sphere.center[2], -8.0f);
    EXPECT_EQ(back.sphere.radius, 16.0f);
    EXPECT_EQ(back.tightRadius, 1.0f);
    EXPECT_EQ(back.firstPatch, 0x41424344u);
}

TEST(FormatRecords, aPatchAndATextureAreTheirFieldsLittleEndian) {
    uint8_t patch[kPatchRecordSize];
    PatchRecord p;
    p.node = 0x01020304;
    p.triangleOffset = 0x11121314;
    p.texture = kNoTexture;
    encodePatchRecord(p, patch);
    EXPECT_EQ(std::vector<uint8_t>(patch, patch + kPatchRecordSize),
              bytesOf({0x04, 0x03, 0x02, 0x01, 0x14, 0x13, 0x12, 0x11, 0xFF, 0xFF, 0xFF, 0xFF}));
    const PatchRecord pb = decodePatchRecord(patch);
    EXPECT_EQ(pb.node, 0x01020304u);
    EXPECT_EQ(pb.triangleOffset, 0x11121314u);
    EXPECT_EQ(pb.texture, kNoTexture);

    uint8_t texture[kTextureRecordSize];
    TextureRecord t;
    t.offset = 0x0102030405060708ull;
    t.length = 0x1112131415161718ull;
    encodeTextureRecord(t, texture);
    EXPECT_EQ(std::vector<uint8_t>(texture, texture + kTextureRecordSize),
              bytesOf({0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11}));
    const TextureRecord tb = decodeTextureRecord(texture);
    EXPECT_EQ(tb.offset, t.offset);
    EXPECT_EQ(tb.length, t.length);
}

// A float is written as the bits it has: a negative zero and a not-a-number are not changed
TEST(FormatRecords, theBitsOfAFloatSurviveTheTrip) {
    NodeRecord n;
    n.error = -0.0f;
    n.tightRadius = std::nanf("0x123");
    uint8_t bytes[kNodeRecordSize];
    encodeNodeRecord(n, bytes);
    const NodeRecord back = decodeNodeRecord(bytes);
    uint32_t a, b, c, d;
    std::memcpy(&a, &n.error, 4);
    std::memcpy(&b, &back.error, 4);
    std::memcpy(&c, &n.tightRadius, 4);
    std::memcpy(&d, &back.tightRadius, 4);
    EXPECT_EQ(a, b);
    EXPECT_EQ(c, d);
    EXPECT_EQ(a, 0x80000000u);
}

TEST(FormatRecords, theTablesOfTheIndexFollowFromTheCounts) {
    // the worked example: one node and the sink, one patch, no texture
    IndexLayout w = indexLayout(2, 1, 0);
    EXPECT_EQ(w.nodeTable, 0u);
    EXPECT_EQ(w.patchTable, 112u);
    EXPECT_EQ(w.textureTable, 128u);  // 124 up to a multiple of 8
    EXPECT_EQ(w.length, 128u);

    // an even number of patches needs no padding
    w = indexLayout(3, 2, 0);
    EXPECT_EQ(w.patchTable, 168u);
    EXPECT_EQ(w.textureTable, 192u);
    EXPECT_EQ(w.length, 192u);

    w = indexLayout(3, 3, 5);
    EXPECT_EQ(w.patchTable, 168u);
    EXPECT_EQ(w.textureTable, 208u);  // 168 + 36 = 204, up to 208
    EXPECT_EQ(w.length, 208u + 5 * 16);
    EXPECT_EQ(w.patchTable % 8, 0u);
    EXPECT_EQ(w.textureTable % 8, 0u);

    // counts that do not fit 32 bits when multiplied do not wrap
    w = indexLayout(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
    EXPECT_EQ(w.patchTable, 56ull * 0xFFFFFFFFull);
    EXPECT_GT(w.length, 0xFFFFFFFFull);
}

TEST(FormatRecords, theAlignmentIsEightBytes) {
    EXPECT_EQ(alignUp(0), 0u);
    EXPECT_EQ(alignUp(1), 8u);
    EXPECT_EQ(alignUp(8), 8u);
    EXPECT_EQ(alignUp(9), 16u);
    EXPECT_EQ(alignUp(0xFFFFFFFFFull), 0x1000000000ull);
}

TEST(FormatRecords, theLayoutsOfVersionOneRoundTripThroughTheSlots) {
    for (int bits = 0; bits < 8; bits++) {
        VertexLayout layout;
        layout.normals = bits & 1;
        layout.colors = bits & 2;
        layout.texCoords = bits & 4;
        AttributeSlot vertex[kAttributeSlots], face[kAttributeSlots];
        layoutToSlots(layout, vertex, face);
        EXPECT_EQ(vertex[0].type, uint8_t(AttributeType::Float));
        EXPECT_EQ(vertex[0].number, 3);
        EXPECT_EQ(face[0].type, uint8_t(AttributeType::UnsignedShort));
        EXPECT_EQ(face[0].number, 3);
        VertexLayout back;
        EXPECT_TRUE(slotsToLayout(vertex, face, back)) << bits;
        EXPECT_TRUE(back == layout) << bits;
        EXPECT_EQ(decodedVertexBytes(layout), 12u + (layout.normals ? 6 : 0) + (layout.colors ? 4 : 0) + (layout.texCoords ? 8 : 0));
    }
}

TEST(FormatRecords, theSlotsOfTheSpecificationHaveTheExactTypes) {
    VertexLayout all;
    all.normals = all.colors = all.texCoords = true;
    AttributeSlot vertex[kAttributeSlots], face[kAttributeSlots];
    layoutToSlots(all, vertex, face);
    EXPECT_EQ(vertex[1].type, 3);  // SHORT x 3
    EXPECT_EQ(vertex[1].number, 3);
    EXPECT_EQ(vertex[2].type, 2);  // UNSIGNED_BYTE x 4
    EXPECT_EQ(vertex[2].number, 4);
    EXPECT_EQ(vertex[3].type, 7);  // FLOAT x 2
    EXPECT_EQ(vertex[3].number, 2);
    for (uint32_t i = 4; i < kAttributeSlots; i++)
        EXPECT_EQ(vertex[i].type, 0);
    for (uint32_t i = 1; i < kAttributeSlots; i++)
        EXPECT_EQ(face[i].type, 0);
}

TEST(FormatRecords, aLayoutThatVersionOneDoesNotAllowIsRefused) {
    VertexLayout all;
    all.normals = all.colors = all.texCoords = true;
    VertexLayout out;
    const auto refused = [&](const char* what, const std::function<void(AttributeSlot*, AttributeSlot*)>& change) {
        AttributeSlot vertex[kAttributeSlots], face[kAttributeSlots];
        layoutToSlots(all, vertex, face);
        change(vertex, face);
        EXPECT_FALSE(slotsToLayout(vertex, face, out)) << what;
    };
    refused("no positions", [](AttributeSlot* v, AttributeSlot*) { v[0] = AttributeSlot(); });
    refused("double positions", [](AttributeSlot* v, AttributeSlot*) { v[0] = {uint8_t(AttributeType::Double), 3}; });
    refused("positions of 2 components", [](AttributeSlot* v, AttributeSlot*) { v[0].number = 2; });
    refused("float normals", [](AttributeSlot* v, AttributeSlot*) { v[1] = {uint8_t(AttributeType::Float), 3}; });
    refused("the colour as BYTE, as the Nexus container wrote it", [](AttributeSlot* v, AttributeSlot*) { v[2].type = uint8_t(AttributeType::Byte); });
    refused("colours of 3 components", [](AttributeSlot* v, AttributeSlot*) { v[2].number = 3; });
    refused("uvs of 3 components", [](AttributeSlot* v, AttributeSlot*) { v[3].number = 3; });
    refused("a fifth attribute", [](AttributeSlot* v, AttributeSlot*) { v[4] = {uint8_t(AttributeType::Float), 1}; });
    refused("the last slot", [](AttributeSlot* v, AttributeSlot*) { v[7] = {uint8_t(AttributeType::Float), 1}; });
    refused("no triangles", [](AttributeSlot*, AttributeSlot* f) { f[0] = AttributeSlot(); });
    refused("32-bit indexes", [](AttributeSlot*, AttributeSlot* f) { f[0].type = uint8_t(AttributeType::UnsignedInt); });
    refused("a face attribute", [](AttributeSlot*, AttributeSlot* f) { f[1] = {uint8_t(AttributeType::Float), 3}; });
    refused("a slot with a number and no type", [](AttributeSlot* v, AttributeSlot*) { v[5].number = 1; });
}

// The roots are the nodes before the smallest node any root links to
TEST(FormatRecords, theRootsAreFoundFromThePatches) {
    // nodes 0 and 1 are roots (they link to 2 and 3), 2 and 3 link to the sink 4
    const std::vector<uint32_t> firstPatch = {0, 1, 2, 3, 4};
    const std::vector<uint32_t> child = {2, 3, 4, 4};
    const auto roots = [&](const std::vector<uint32_t>& first, const std::vector<uint32_t>& links) {
        return countRoots(
            uint32_t(first.size()), [&](uint32_t n) { return first[n]; }, [&](uint32_t p) { return links[p]; });
    };
    EXPECT_EQ(roots(firstPatch, child), 2u);
    // one root with two children, all leaves
    EXPECT_EQ(roots({0, 2, 3, 4}, {1, 2, 3, 3}), 1u);
    // a model of one node and the sink
    EXPECT_EQ(roots({0, 1}, {1}), 1u);
    // the root links to a node that links back to the sink only
    EXPECT_EQ(roots({0, 1, 2}, {1, 2}), 1u);
}

}  // namespace
