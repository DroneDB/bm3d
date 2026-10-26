// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bm3d_reader.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

#include <corto/decoder.h>

#include "../common/pathutil.h"
#include "metadata.h"
#include "sha256.h"

namespace bm3d::detail {

namespace {

// --- sources -----------------------------------------------------------------------------------

class FileSource : public ByteSource {
public:
    explicit FileSource(const std::string& path) : path_(path) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(nativePath(path), ec);
        if (ec)
            throw FormatError(FormatErrorKind::Io, "could not open " + path + ": " + ec.message());
        size_ = size;
        stream_.open(nativePath(path), std::ios::in | std::ios::binary);
        if (!stream_.is_open())
            throw FormatError(FormatErrorKind::Io, "could not open " + path);
    }

    uint64_t size() const override { return size_; }

    void read(uint64_t offset, void* where, uint64_t length) override {
        stream_.clear();
        stream_.seekg(std::streamoff(offset));
        stream_.read(static_cast<char*>(where), std::streamsize(length));
        if (!stream_ || uint64_t(stream_.gcount()) != length)
            throw FormatError(FormatErrorKind::Io, "could not read " + std::to_string(length) + " bytes at " +
                                                       std::to_string(offset) + " of " + path_);
    }

private:
    std::string path_;
    std::ifstream stream_;
    uint64_t size_ = 0;
};

class MemorySource : public ByteSource {
public:
    explicit MemorySource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

    uint64_t size() const override { return bytes_.size(); }

    void read(uint64_t offset, void* where, uint64_t length) override {
        if (offset > bytes_.size() || length > bytes_.size() - offset)
            throw FormatError(FormatErrorKind::Io, "reading past the end of the file");
        if (length > 0)
            std::memcpy(where, bytes_.data() + offset, size_t(length));
    }

private:
    std::vector<uint8_t> bytes_;
};

// --- messages ----------------------------------------------------------------------------------

[[noreturn]] void fail(FormatErrorKind kind, const std::string& message) {
    throw FormatError(kind, message);
}

std::string str(uint64_t value) {
    return std::to_string(value);
}

// A float as the shortest text that reads back to it ("nan", "inf" for those)
std::string strFloat(float value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9) << value;
    return out.str();
}

std::string hex(uint32_t value) {
    static const char digits[] = "0123456789abcdef";
    std::string out = "0x";
    for (int shift = 28; shift >= 0; shift -= 4)
        out += digits[(value >> shift) & 15];
    return out;
}

// Whether [offset, offset + length) lies inside [0, limit), with arithmetic that cannot wrap
bool inside(uint64_t offset, uint64_t length, uint64_t limit) {
    return offset <= limit && length <= limit - offset;
}

// --- spheres -------------------------------------------------------------------------------------

bool finiteSphere(const SphereRecord& sphere) {
    return std::isfinite(sphere.center[0]) && std::isfinite(sphere.center[1]) && std::isfinite(sphere.center[2]) &&
           std::isfinite(sphere.radius);
}

std::string describe(const SphereRecord& sphere) {
    return "centre (" + strFloat(sphere.center[0]) + ", " + strFloat(sphere.center[1]) + ", " +
           strFloat(sphere.center[2]) + ") radius " + strFloat(sphere.radius);
}

// --- the ranges of a file that must not overlap ------------------------------------------------

struct Range {
    uint64_t begin;
    uint64_t end;
    std::string what;
};

std::string describe(const Range& r) {
    return r.what + " [" + str(r.begin) + ", " + str(r.end) + ")";
}

}  // namespace

Bm3dReader::Bm3dReader(std::unique_ptr<ByteSource> source) : source_(std::move(source)) {
    openChecks();
}

Bm3dReader::Bm3dReader(Bm3dReader&&) noexcept = default;
Bm3dReader& Bm3dReader::operator=(Bm3dReader&&) noexcept = default;
Bm3dReader::~Bm3dReader() = default;

Bm3dReader Bm3dReader::open(const std::string& path) {
    return Bm3dReader(std::make_unique<FileSource>(path));
}

Bm3dReader Bm3dReader::fromBytes(std::vector<uint8_t> bytes) {
    return Bm3dReader(std::make_unique<MemorySource>(std::move(bytes)));
}

uint64_t Bm3dReader::fileSize() const {
    return source_->size();
}

void Bm3dReader::openChecks() {
    const uint64_t size = source_->size();
    if (size < kHeaderSize)
        fail(FormatErrorKind::TruncatedHeader,
             "the file has " + str(size) + " bytes and the header takes " + str(kHeaderSize));
    uint8_t bytes[kHeaderSize];
    source_->read(0, bytes, kHeaderSize);
    header_ = decodeHeaderRecord(bytes);
    const HeaderRecord& h = header_;

    if (std::memcmp(h.magic, kMagic, 4) != 0) {
        // the Nexus container begins with the word 0x4E787320, "Nxs ", written as 20 73 78 4E
        const bool nexus = h.magic[0] == 0x20 && h.magic[1] == 0x73 && h.magic[2] == 0x78 && h.magic[3] == 0x4E;
        fail(FormatErrorKind::BadMagic,
             nexus ? "the file is a Nexus file (format 0 of the Nexus project), not a .bm3d file"
                   : "the file does not begin with BM3D: " + hex(uint32_t(h.magic[0]) | (uint32_t(h.magic[1]) << 8) |
                                                                  (uint32_t(h.magic[2]) << 16) |
                                                                  (uint32_t(h.magic[3]) << 24)) +
                         " (the first four bytes as a little-endian number)");
    }
    if (h.formatVersion == 0 || h.formatVersion > kFormatVersion)
        fail(FormatErrorKind::UnsupportedVersion, "the file is version " + str(h.formatVersion) +
                                                      " and this reader reads up to version " + str(kFormatVersion));
    if (h.headerSize < kHeaderSize || h.headerSize % kAlignment != 0 || h.headerSize > size)
        fail(FormatErrorKind::BadHeaderSize, "the header size is " + str(h.headerSize) +
                                                 " (at least " + str(kHeaderSize) + ", a multiple of 8, and no more than the " +
                                                 str(size) + " bytes of the file)");
    if (!(h.flags & kFlagCortoCompressed))
        fail(FormatErrorKind::CompressionFlagMissing,
             "the nodes are not corto streams (flags " + hex(h.flags) + "): this format has no other form");
    if (h.flags & ~kKnownFlags)
        fail(FormatErrorKind::UnknownFlags, "the flags " + hex(h.flags) + " set bits that version 1 does not define");
    if (bool(h.flags & kFlagTextureWebp) != (h.textureCount > 0))
        fail(FormatErrorKind::UnknownFlags, "the flags " + hex(h.flags) + " and the " + str(h.textureCount) +
                                                " textures disagree: the WebP flag is set if and only if there are textures");
    if (!slotsToLayout(h.vertexLayout, h.faceLayout, layout_))
        fail(FormatErrorKind::UnsupportedLayout, "the vertex or face layout is not one of version 1");
    if (layout_.texCoords != (h.textureCount > 0))
        fail(FormatErrorKind::UnsupportedLayout,
             "the texture coordinates are " + std::string(layout_.texCoords ? "there" : "missing") + " and the file has " +
                 str(h.textureCount) + " textures");
    if (h.nodeCount < 2)
        fail(FormatErrorKind::BadCounts, "the file has " + str(h.nodeCount) + " nodes and the sink is one of them");
    if (!finiteSphere(h.sphere))
        fail(FormatErrorKind::BadSphere, "the sphere of the header, " + describe(h.sphere) + ", is not finite");

    checkIndexRanges();

    std::vector<uint8_t> index(static_cast<size_t>(h.indexLength));
    source_->read(h.indexOffset, index.data(), index.size());
    const IndexLayout where = indexLayout(h.nodeCount, h.patchCount, h.textureCount);
    nodes_.reserve(h.nodeCount);
    for (uint32_t i = 0; i < h.nodeCount; i++)
        nodes_.push_back(decodeNodeRecord(index.data() + where.nodeTable + uint64_t(i) * kNodeRecordSize));
    patches_.reserve(h.patchCount);
    for (uint32_t i = 0; i < h.patchCount; i++)
        patches_.push_back(decodePatchRecord(index.data() + where.patchTable + uint64_t(i) * kPatchRecordSize));
    textures_.reserve(h.textureCount);
    for (uint32_t i = 0; i < h.textureCount; i++)
        textures_.push_back(decodeTextureRecord(index.data() + where.textureTable + uint64_t(i) * kTextureRecordSize));

    checkIndex();

    rootCount_ = countRoots(
        h.nodeCount, [this](uint32_t node) { return nodes_[node].firstPatch; },
        [this](uint32_t patch) { return patches_[patch].node; });
}

// Section 11, "out of range" and "overlapping sections"
void Bm3dReader::checkIndexRanges() {
    const HeaderRecord& h = header_;
    const uint64_t size = source_->size();
    const IndexLayout where = indexLayout(h.nodeCount, h.patchCount, h.textureCount);

    if (h.indexOffset % kAlignment != 0 || h.indexOffset < h.headerSize)
        fail(FormatErrorKind::OutOfRange, "the index starts at " + str(h.indexOffset) +
                                              ", which is not a multiple of 8 that is at least the header size " + str(h.headerSize));
    if (h.indexLength != where.length)
        fail(FormatErrorKind::OutOfRange, "the index is " + str(h.indexLength) + " bytes and the counts need " + str(where.length));
    if (!inside(h.indexOffset, h.indexLength, size))
        fail(FormatErrorKind::OutOfRange, "the index [" + str(h.indexOffset) + ", " + str(h.indexOffset) + " + " +
                                              str(h.indexLength) + ") is not inside the file of " + str(size) + " bytes");
    const uint64_t dataStart = h.indexOffset + h.indexLength;
    if (h.metadataOffset % kAlignment != 0 || h.metadataOffset < dataStart)
        fail(FormatErrorKind::OutOfRange, "the metadata starts at " + str(h.metadataOffset) +
                                              ", which is not a multiple of 8 that is at least the end of the index " + str(dataStart));
    if (!inside(h.metadataOffset, h.metadataLength, size))
        fail(FormatErrorKind::OutOfRange, "the metadata [" + str(h.metadataOffset) + ", " + str(h.metadataOffset) + " + " +
                                              str(h.metadataLength) + ") is not inside the file of " + str(size) +
                                              " bytes (the file is truncated)");
}

void Bm3dReader::checkIndex() {
    const HeaderRecord& h = header_;
    const uint32_t sinkNode = h.nodeCount - 1;
    const uint64_t dataStart = h.indexOffset + h.indexLength;

    // every payload and texture lies in the data between the index and the metadata
    const auto checkRange = [&](const std::string& what, uint64_t offset, uint64_t length) {
        if (offset % kAlignment != 0)
            fail(FormatErrorKind::OutOfRange, what + " starts at " + str(offset) + ", which is not a multiple of 8");
        if (length == 0)
            fail(FormatErrorKind::OutOfRange, what + " is empty");
        if (offset < dataStart || !inside(offset, length, h.metadataOffset))
            fail(FormatErrorKind::OutOfRange, what + " [" + str(offset) + ", " + str(offset) + " + " + str(length) +
                                                  ") is not inside the data [" + str(dataStart) + ", " + str(h.metadataOffset) + ")");
    };
    for (uint32_t i = 0; i < sinkNode; i++)
        checkRange("the payload of node " + str(i), nodes_[i].payloadOffset, nodes_[i].payloadLength);
    for (uint32_t i = 0; i < h.textureCount; i++)
        checkRange("texture " + str(i), textures_[i].offset, textures_[i].length);

    // no two sections overlap: sorted by their start, a section that begins before the one
    // before it ends overlaps it
    std::vector<Range> ranges;
    ranges.push_back({0, h.headerSize, "the header"});
    ranges.push_back({h.indexOffset, h.indexOffset + h.indexLength, "the index"});
    for (uint32_t i = 0; i < sinkNode; i++)
        ranges.push_back({nodes_[i].payloadOffset, nodes_[i].payloadOffset + nodes_[i].payloadLength,
                          "the payload of node " + str(i)});
    for (uint32_t i = 0; i < h.textureCount; i++)
        ranges.push_back({textures_[i].offset, textures_[i].offset + textures_[i].length, "texture " + str(i)});
    if (h.metadataLength > 0)
        ranges.push_back({h.metadataOffset, h.metadataOffset + h.metadataLength, "the metadata"});
    std::stable_sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.begin < b.begin; });
    for (size_t i = 1; i < ranges.size(); i++)
        if (ranges[i].begin < ranges[i - 1].end)
            fail(FormatErrorKind::OverlappingSections, describe(ranges[i - 1]) + " and " + describe(ranges[i]) + " overlap");

    // the index (docs/FORMAT.md, section 11, the rules of the index): rule by rule, each one over the
    // whole table before the next, so that the node or patch reported is the one every reader reports
    const auto bad = [](const std::string& message) { fail(FormatErrorKind::BadIndex, message); };
    if (nodes_[0].firstPatch != 0)
        bad("the first patch of node 0 is " + str(nodes_[0].firstPatch) + " and not 0");
    for (uint32_t i = 0; i + 1 < h.nodeCount; i++)
        if (nodes_[i + 1].firstPatch < nodes_[i].firstPatch)
            bad("the first patch decreases from node " + str(i) + " (" + str(nodes_[i].firstPatch) + ") to node " +
                str(i + 1) + " (" + str(nodes_[i + 1].firstPatch) + ")");
    if (nodes_[sinkNode].firstPatch != h.patchCount)
        bad("the first patch of the sink is " + str(nodes_[sinkNode].firstPatch) + " and the file has " +
            str(h.patchCount) + " patches");

    // every byte of the sink but its first patch is zero (a float field is +0, not -0)
    uint8_t sinkBytes[kNodeRecordSize];
    encodeNodeRecord(nodes_[sinkNode], sinkBytes);
    if (std::any_of(sinkBytes, sinkBytes + kNodeRecordSize - 4, [](uint8_t b) { return b != 0; }))
        bad("the sink (node " + str(sinkNode) + ") has a field other than the first patch that is not 0");

    for (uint32_t i = 0; i < sinkNode; i++)
        if (patchEnd(i) == patchBegin(i))
            bad("node " + str(i) + " has no patch");
    for (uint32_t i = 0; i < sinkNode; i++)
        if (nodes_[i].faceCount == 0 || nodes_[i].vertexCount < 3)
            bad("node " + str(i) + " has " + str(nodes_[i].vertexCount) + " vertices and " + str(nodes_[i].faceCount) +
                " triangles");
    for (uint32_t i = 0; i < sinkNode; i++) {
        const NodeRecord& n = nodes_[i];
        if (!std::isfinite(n.error) || !finiteSphere(n.sphere) || !std::isfinite(n.tightRadius))
            bad("node " + str(i) + " has a value that is not finite: error " + strFloat(n.error) + ", sphere " +
                describe(n.sphere) + ", tight radius " + strFloat(n.tightRadius));
    }
    for (uint32_t i = 0; i < sinkNode; i++)
        for (uint32_t p = patchBegin(i); p < patchEnd(i); p++)
            if (patches_[p].node >= h.nodeCount)
                bad("patch " + str(p) + " of node " + str(i) + " names node " + str(patches_[p].node) +
                    " and the file has " + str(h.nodeCount));
    for (uint32_t i = 0; i < sinkNode; i++)
        for (uint32_t p = patchBegin(i); p < patchEnd(i); p++)
            if (patches_[p].node != sinkNode && patches_[p].node <= i)
                bad("patch " + str(p) + " of node " + str(i) + " names node " + str(patches_[p].node) +
                    ", which is not stored after it");
    for (uint32_t i = 0; i < sinkNode; i++) {
        uint32_t leaves = 0;
        for (uint32_t p = patchBegin(i); p < patchEnd(i); p++)
            leaves += patches_[p].node == sinkNode;
        if (leaves != 0 && leaves != patchEnd(i) - patchBegin(i))
            bad("node " + str(i) + " has patches that name the sink and patches that do not");
    }
    for (uint32_t i = 0; i < sinkNode; i++)
        for (uint32_t p = patchBegin(i) + 1; p < patchEnd(i); p++)
            if (patches_[p].triangleOffset < patches_[p - 1].triangleOffset)
                bad("the triangle offset of patch " + str(p) + " of node " + str(i) + " (" + str(patches_[p].triangleOffset) +
                    ") is below the one before it (" + str(patches_[p - 1].triangleOffset) + ")");
    for (uint32_t i = 0; i < sinkNode; i++)
        if (patches_[patchEnd(i) - 1].triangleOffset != nodes_[i].faceCount)
            bad("the last triangle offset of node " + str(i) + " is " + str(patches_[patchEnd(i) - 1].triangleOffset) +
                " and the node has " + str(nodes_[i].faceCount) + " triangles");
    for (uint32_t i = 0; i < sinkNode; i++)
        for (uint32_t p = patchBegin(i); p < patchEnd(i); p++)
            if (patches_[p].texture != kNoTexture && patches_[p].texture >= h.textureCount)
                bad("patch " + str(p) + " of node " + str(i) + " names texture " + str(patches_[p].texture) +
                    " and the file has " + str(h.textureCount));
    for (uint32_t i = 0; i < sinkNode; i++)
        for (uint32_t p = patchBegin(i); p < patchEnd(i); p++)
            if (patches_[p].texture != patches_[patchBegin(i)].texture)
                bad("node " + str(i) + " has patches with different textures");
    for (uint32_t i = 0; i < sinkNode; i++)
        for (uint32_t p = patchBegin(i); p < patchEnd(i); p++)
            if ((patches_[p].texture != kNoTexture) != layout_.texCoords)
                bad("patch " + str(p) + " of node " + str(i) +
                    (layout_.texCoords ? " has no texture and the layout has texture coordinates"
                                       : " has a texture and the layout has no texture coordinates"));
}

std::vector<uint8_t> Bm3dReader::readNodePayload(uint32_t node) {
    if (node >= header_.nodeCount - 1)
        throw Error("node " + str(node) + " is not a real node of the file");
    const NodeRecord& n = nodes_[node];
    std::vector<uint8_t> bytes(static_cast<size_t>(n.payloadLength));
    source_->read(n.payloadOffset, bytes.data(), bytes.size());
    return bytes;
}

DecodedNode Bm3dReader::decodeNode(uint32_t node) {
    const std::vector<uint8_t> payload = readNodePayload(node);
    const NodeRecord& record = nodes_[node];
    const std::string who = "node " + str(node);
    if (payload.size() > INT_MAX)
        fail(FormatErrorKind::CorruptNode, who + " has a stream of " + str(payload.size()) + " bytes");

    DecodedNode out;
    try {
        crt::Decoder decoder(int(payload.size()), payload.data());
        if (decoder.nvert != record.vertexCount || decoder.nface != record.faceCount)
            fail(FormatErrorKind::CorruptNode, who + " has " + str(record.vertexCount) + " vertices and " + str(record.faceCount) +
                                                   " triangles in the index and " + str(decoder.nvert) + " and " +
                                                   str(decoder.nface) + " in its stream");
        if (!decoder.hasAttr("position") || decoder.hasAttr("normal") != layout_.normals ||
            decoder.hasAttr("color") != layout_.colors || decoder.hasAttr("uv") != layout_.texCoords)
            fail(FormatErrorKind::CorruptNode, who + " has attributes that are not the ones of the layout");
        const size_t nv = record.vertexCount;
        out.positions.resize(3 * nv);
        decoder.setPositions(out.positions.data());
        if (layout_.texCoords) {
            out.uvs.resize(2 * nv);
            decoder.setUvs(out.uvs.data());
        }
        if (layout_.normals) {
            out.normals.resize(3 * nv);
            decoder.setNormals(out.normals.data());
        }
        if (layout_.colors) {
            out.colors.resize(4 * nv);
            decoder.setColors(out.colors.data(), 4);
        }
        out.indices.resize(3 * size_t(record.faceCount));
        decoder.setIndex(out.indices.data());
        decoder.decode();
        for (const crt::Group& group : decoder.index.groups)
            out.groupEnds.push_back(group.end);
    } catch (const FormatError&) {
        throw;
    } catch (const char* message) {  // corto reports its errors as C strings
        fail(FormatErrorKind::CorruptNode, who + " cannot be decoded: " + message);
    } catch (const std::exception& e) {
        fail(FormatErrorKind::CorruptNode, who + " cannot be decoded: " + e.what());
    }
    return out;
}

std::vector<uint8_t> Bm3dReader::readTexture(uint32_t texture) {
    if (texture >= header_.textureCount)
        throw Error("texture " + str(texture) + " is not in the file");
    const TextureRecord& t = textures_[texture];
    std::vector<uint8_t> bytes(static_cast<size_t>(t.length));
    source_->read(t.offset, bytes.data(), bytes.size());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WEBP", 4) != 0)
        fail(FormatErrorKind::CorruptTexture, "texture " + str(texture) + " is not a RIFF/WEBP image");
    return bytes;
}

std::string Bm3dReader::metadata() {
    const uint64_t length = header_.metadataLength;
    if (length == 0)
        return "{}";
    if (length > kMaxMetadataLength)
        fail(FormatErrorKind::BadMetadata, "the metadata is " + str(length) + " bytes, more than the " +
                                               str(kMaxMetadataLength) + " this reader accepts");
    std::string text(static_cast<size_t>(length), '\0');
    source_->read(header_.metadataOffset, &text[0], length);
    const std::string problem = metadataBlockError(text);
    if (!problem.empty())
        fail(FormatErrorKind::BadMetadata, "the metadata is wrong: " + problem);
    return text;
}

std::string Bm3dReader::contentHash() {
    const uint64_t size = source_->size();
    const HeaderRecord& h = header_;
    Sha256 hash;
    std::vector<uint8_t> buffer(1 << 20);
    // the bytes [from, to) of the file
    const auto feed = [&](uint64_t from, uint64_t to) {
        while (from < to) {
            const size_t count = static_cast<size_t>(std::min<uint64_t>(buffer.size(), to - from));
            source_->read(from, buffer.data(), count);
            hash.update(buffer.data(), count);
            from += count;
        }
    };
    feed(0, kMetadataLengthOffset);
    const uint8_t zeros[8] = {};
    hash.update(zeros, sizeof(zeros));
    feed(kMetadataLengthOffset + 8, h.metadataOffset);
    feed(h.metadataOffset + h.metadataLength, size);
    return hash.finishHex();
}

}  // namespace bm3d::detail
