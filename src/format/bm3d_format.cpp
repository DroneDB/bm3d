// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bm3d_format.h"

namespace bm3d::detail {

namespace {

// Writes and reads numbers at a moving position, little endian whatever the machine is
class Putter {
public:
    explicit Putter(uint8_t* bytes) : at_(bytes) {}

    void u8(uint8_t v) { *at_++ = v; }
    void u16(uint16_t v) { le(v, 2); }
    void i16(int16_t v) { le(static_cast<uint16_t>(v), 2); }
    void u32(uint32_t v) { le(v, 4); }
    void u64(uint64_t v) { le(v, 8); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void slot(const AttributeSlot& s) {
        u8(s.type);
        u8(s.number);
    }
    void sphere(const SphereRecord& s) {
        for (float c : s.center)
            f32(c);
        f32(s.radius);
    }

private:
    void le(uint64_t v, int size) {
        for (int i = 0; i < size; i++)
            *at_++ = static_cast<uint8_t>(v >> (8 * i));
    }
    uint8_t* at_;
};

class Getter {
public:
    explicit Getter(const uint8_t* bytes) : at_(bytes) {}

    uint8_t u8() { return *at_++; }
    uint16_t u16() { return static_cast<uint16_t>(le(2)); }
    int16_t i16() { return static_cast<int16_t>(static_cast<uint16_t>(le(2))); }
    uint32_t u32() { return static_cast<uint32_t>(le(4)); }
    uint64_t u64() { return le(8); }
    float f32() {
        const uint32_t bits = u32();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    AttributeSlot slot() {
        AttributeSlot s;
        s.type = u8();
        s.number = u8();
        return s;
    }
    SphereRecord sphere() {
        SphereRecord s;
        for (float& c : s.center)
            c = f32();
        s.radius = f32();
        return s;
    }

private:
    uint64_t le(int size) {
        uint64_t v = 0;
        for (int i = 0; i < size; i++)
            v |= static_cast<uint64_t>(*at_++) << (8 * i);
        return v;
    }
    const uint8_t* at_;
};

bool isSlot(const AttributeSlot& slot, AttributeType type, uint8_t number) {
    return slot.type == static_cast<uint8_t>(type) && slot.number == number;
}

bool isEmpty(const AttributeSlot& slot) {
    return slot.type == 0 && slot.number == 0;
}

}  // namespace

void encodeHeaderRecord(const HeaderRecord& h, uint8_t* bytes) {
    Putter out(bytes);
    for (uint8_t m : h.magic)
        out.u8(m);
    out.u32(h.formatVersion);
    out.u32(h.headerSize);
    out.u32(h.flags);
    for (const AttributeSlot& s : h.vertexLayout)
        out.slot(s);
    for (const AttributeSlot& s : h.faceLayout)
        out.slot(s);
    out.u64(h.vertexCount);
    out.u64(h.faceCount);
    out.u64(h.metadataOffset);
    out.u64(h.metadataLength);
    out.u64(h.indexOffset);
    out.u64(h.indexLength);
    out.u32(h.nodeCount);
    out.u32(h.patchCount);
    out.u32(h.textureCount);
    out.sphere(h.sphere);
    for (uint8_t r : h.reserved)
        out.u8(r);
}

HeaderRecord decodeHeaderRecord(const uint8_t* bytes) {
    Getter in(bytes);
    HeaderRecord h;
    for (uint8_t& m : h.magic)
        m = in.u8();
    h.formatVersion = in.u32();
    h.headerSize = in.u32();
    h.flags = in.u32();
    for (AttributeSlot& s : h.vertexLayout)
        s = in.slot();
    for (AttributeSlot& s : h.faceLayout)
        s = in.slot();
    h.vertexCount = in.u64();
    h.faceCount = in.u64();
    h.metadataOffset = in.u64();
    h.metadataLength = in.u64();
    h.indexOffset = in.u64();
    h.indexLength = in.u64();
    h.nodeCount = in.u32();
    h.patchCount = in.u32();
    h.textureCount = in.u32();
    h.sphere = in.sphere();
    for (uint8_t& r : h.reserved)
        r = in.u8();
    return h;
}

void encodeNodeRecord(const NodeRecord& n, uint8_t* bytes) {
    Putter out(bytes);
    out.u64(n.payloadOffset);
    out.u64(n.payloadLength);
    out.f32(n.error);
    out.u16(n.vertexCount);
    out.u16(n.faceCount);
    for (int16_t c : n.cone)
        out.i16(c);
    out.sphere(n.sphere);
    out.f32(n.tightRadius);
    out.u32(n.firstPatch);
}

NodeRecord decodeNodeRecord(const uint8_t* bytes) {
    Getter in(bytes);
    NodeRecord n;
    n.payloadOffset = in.u64();
    n.payloadLength = in.u64();
    n.error = in.f32();
    n.vertexCount = in.u16();
    n.faceCount = in.u16();
    for (int16_t& c : n.cone)
        c = in.i16();
    n.sphere = in.sphere();
    n.tightRadius = in.f32();
    n.firstPatch = in.u32();
    return n;
}

void encodePatchRecord(const PatchRecord& p, uint8_t* bytes) {
    Putter out(bytes);
    out.u32(p.node);
    out.u32(p.triangleOffset);
    out.u32(p.texture);
}

PatchRecord decodePatchRecord(const uint8_t* bytes) {
    Getter in(bytes);
    PatchRecord p;
    p.node = in.u32();
    p.triangleOffset = in.u32();
    p.texture = in.u32();
    return p;
}

void encodeTextureRecord(const TextureRecord& t, uint8_t* bytes) {
    Putter out(bytes);
    out.u64(t.offset);
    out.u64(t.length);
}

TextureRecord decodeTextureRecord(const uint8_t* bytes) {
    Getter in(bytes);
    TextureRecord t;
    t.offset = in.u64();
    t.length = in.u64();
    return t;
}

size_t decodedVertexBytes(const VertexLayout& layout) {
    return 12 + (layout.texCoords ? 8 : 0) + (layout.normals ? 6 : 0) + (layout.colors ? 4 : 0);
}

void layoutToSlots(const VertexLayout& layout, AttributeSlot vertex[kAttributeSlots], AttributeSlot face[kAttributeSlots]) {
    for (uint32_t i = 0; i < kAttributeSlots; i++)
        vertex[i] = face[i] = AttributeSlot();
    vertex[0] = {static_cast<uint8_t>(AttributeType::Float), 3};
    if (layout.normals)
        vertex[1] = {static_cast<uint8_t>(AttributeType::Short), 3};
    if (layout.colors)
        vertex[2] = {static_cast<uint8_t>(AttributeType::UnsignedByte), 4};
    if (layout.texCoords)
        vertex[3] = {static_cast<uint8_t>(AttributeType::Float), 2};
    face[0] = {static_cast<uint8_t>(AttributeType::UnsignedShort), 3};
}

bool slotsToLayout(const AttributeSlot vertex[kAttributeSlots], const AttributeSlot face[kAttributeSlots], VertexLayout& layout) {
    if (!isSlot(vertex[0], AttributeType::Float, 3) || !isSlot(face[0], AttributeType::UnsignedShort, 3))
        return false;
    for (uint32_t i = 4; i < kAttributeSlots; i++)
        if (!isEmpty(vertex[i]))
            return false;
    for (uint32_t i = 1; i < kAttributeSlots; i++)
        if (!isEmpty(face[i]))
            return false;
    layout = VertexLayout();
    if (!isEmpty(vertex[1])) {
        if (!isSlot(vertex[1], AttributeType::Short, 3))
            return false;
        layout.normals = true;
    }
    if (!isEmpty(vertex[2])) {
        if (!isSlot(vertex[2], AttributeType::UnsignedByte, 4))
            return false;
        layout.colors = true;
    }
    if (!isEmpty(vertex[3])) {
        if (!isSlot(vertex[3], AttributeType::Float, 2))
            return false;
        layout.texCoords = true;
    }
    return true;
}

IndexLayout indexLayout(uint32_t nodeCount, uint32_t patchCount, uint32_t textureCount) {
    IndexLayout layout;
    layout.nodeTable = 0;
    layout.patchTable = uint64_t(kNodeRecordSize) * nodeCount;
    layout.textureTable = alignUp(layout.patchTable + uint64_t(kPatchRecordSize) * patchCount);
    layout.length = layout.textureTable + uint64_t(kTextureRecordSize) * textureCount;
    return layout;
}

}  // namespace bm3d::detail
