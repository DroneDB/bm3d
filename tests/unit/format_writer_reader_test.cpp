// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bm3dWriter and Bm3dReader (src/format): the file the writer produces against the worked example
// of docs/FORMAT.md byte for byte, the round trip, every check a reader makes when it opens a file
// (with hand-made bad files, in the order of the specification), the version rule, the metadata
// block, and the content hash.

#include <algorithm>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format/bm3d_reader.h"
#include "format/bm3d_writer.h"
#include "format/sha256.h"
#include "model_files.h"
#include "temp_dir.h"

namespace {

using namespace bm3d::detail;
using bm3dtest::pathToUtf8;
using bm3dtest::readBinaryFile;
using bm3dtest::TempDir;

using Bytes = std::vector<uint8_t>;

Bytes fromHex(const std::string& text) {
    Bytes out;
    std::istringstream in(text);
    std::string byte;
    while (in >> byte)
        out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
    return out;
}

Bytes slice(const Bytes& b, size_t from, size_t length) {
    return Bytes(b.begin() + std::ptrdiff_t(from), b.begin() + std::ptrdiff_t(from + length));
}

Bytes readFile(const std::string& path) {
    const std::string s = readBinaryFile(bm3dtest::pathFromUtf8(path));
    return Bytes(s.begin(), s.end());
}

// --- the worked example of the specification ---------------------------------------------------

// The metadata of the example, 363 bytes
const char* kExampleMetadata =
    "{\"generator\":\"bm3d 1.0.0\",\"buildOptions\":{\"threads\":1,\"nodeFaces\":16384,\"topNodeFaces\":4096,"
    "\"textureQuality\":90,\"maxNodeTextureSide\":1024,\"scaling\":0.5,\"adaptive\":0.333,\"skipLevels\":-1,"
    "\"errorQ\":0.1,\"normalBits\":10,\"lumaBits\":6,\"chromaBits\":6,\"alphaBits\":5,\"texStep\":0.25},"
    "\"source\":{\"name\":\"quad.obj\",\"vertices\":4,\"faces\":2,\"bounds\":{\"min\":[0,0,0],\"max\":[1,1,0]}}}";

Bytes writeExample(const TempDir& dir, const std::string& metadata = kExampleMetadata) {
    VertexLayout layout;
    layout.normals = true;
    Bm3dWriter writer(pathToUtf8(dir.file("quad.bm3d")), layout, {1, 1, 0});

    NodeRecord node;
    node.error = 0.70710677f;
    node.vertexCount = 4;
    node.faceCount = 2;
    node.cone[2] = 32766;
    node.cone[3] = 32760;
    node.sphere.center[0] = 0.5f;
    node.sphere.center[1] = 0.5f;
    node.sphere.radius = 0.70710677f;
    node.tightRadius = 0.70710677f;
    PatchRecord patch;
    patch.node = 1;  // the sink
    patch.triangleOffset = 2;
    Bytes stream(120);
    for (size_t i = 0; i < stream.size(); i++)
        stream[i] = uint8_t(i);
    writer.addNode(node, &patch, 1, stream.data(), stream.size());

    SphereRecord sphere = node.sphere;
    writer.finish(sphere, metadata);
    return readFile(pathToUtf8(dir.file("quad.bm3d")));
}

TEST(FormatWriter, theFileOfTheWorkedExampleIsTheBytesOfTheSpecification) {
    TempDir dir;
    const Bytes file = writeExample(dir);
    ASSERT_EQ(file.size(), 739u);

    // the header, the 128 bytes of the document
    const Bytes header = fromHex(
        "42 4d 33 44 01 00 00 00 80 00 00 00 01 00 00 00 "
        "07 03 03 03 00 00 00 00 00 00 00 00 00 00 00 00 "
        "04 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
        "04 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00 "
        "78 01 00 00 00 00 00 00 6b 01 00 00 00 00 00 00 "
        "80 00 00 00 00 00 00 00 80 00 00 00 00 00 00 00 "
        "02 00 00 00 01 00 00 00 00 00 00 00 00 00 00 3f "
        "00 00 00 3f 00 00 00 00 f3 04 35 3f 00 00 00 00");
    ASSERT_EQ(header.size(), 128u);
    EXPECT_EQ(slice(file, 0, 128), header);

    // node 0, at 128
    EXPECT_EQ(slice(file, 128, 56), fromHex(
        "00 01 00 00 00 00 00 00 78 00 00 00 00 00 00 00 "
        "f3 04 35 3f 04 00 02 00 00 00 00 00 fe 7f f8 7f "
        "00 00 00 3f 00 00 00 3f 00 00 00 00 f3 04 35 3f "
        "f3 04 35 3f 00 00 00 00"));
    // the sink, at 184: 52 zero bytes and firstPatch = patchCount
    Bytes sink(56, 0);
    sink[52] = 1;
    EXPECT_EQ(slice(file, 184, 56), sink);
    // the patch, at 240, and the 4 zero bytes that pad the patch table to the texture table
    EXPECT_EQ(slice(file, 240, 16), fromHex("01 00 00 00 02 00 00 00 ff ff ff ff 00 00 00 00"));
    // the payload, at 256, 120 bytes, and the metadata at 376
    EXPECT_EQ(file[256], 0);
    EXPECT_EQ(file[256 + 119], 119);
    EXPECT_EQ(std::string(file.begin() + 376, file.end()), kExampleMetadata);
    EXPECT_EQ(std::string(kExampleMetadata).size(), 363u);
}

TEST(FormatWriter, theFileOfTheWorkedExampleIsReadBack) {
    TempDir dir;
    writeExample(dir);
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("quad.bm3d")));
    const HeaderRecord& h = reader.header();
    EXPECT_EQ(h.formatVersion, 1u);
    EXPECT_EQ(h.flags, 1u);
    EXPECT_EQ(h.vertexCount, 4u);
    EXPECT_EQ(h.faceCount, 2u);
    EXPECT_EQ(h.nodeCount, 2u);
    EXPECT_EQ(h.metadataOffset, 376u);
    EXPECT_EQ(h.metadataLength, 363u);
    EXPECT_TRUE(reader.layout().normals);
    EXPECT_FALSE(reader.layout().colors);
    EXPECT_FALSE(reader.layout().texCoords);
    EXPECT_EQ(reader.rootCount(), 1u);
    EXPECT_TRUE(reader.isLeaf(0));
    EXPECT_EQ(reader.nodeTexture(0), -1);
    EXPECT_EQ(reader.nodes()[0].cone[3], 32760);
    EXPECT_EQ(reader.readNodePayload(0).size(), 120u);
    EXPECT_EQ(reader.metadata(), kExampleMetadata);
}

// --- a model of three nodes, with and without textures -----------------------------------------

constexpr uint32_t kSink = 3;

struct Sample {
    bool textured;
    Bytes bytes;
    std::vector<Bytes> payloads;
    std::vector<Bytes> images;
};

Bytes fakePayload(size_t length, uint8_t fill) {
    return Bytes(length, fill);
}

Bytes fakeImage(size_t length, uint8_t fill) {
    Bytes image(length, fill);
    std::memcpy(image.data(), "RIFF", 4);
    std::memcpy(image.data() + 8, "WEBP", 4);
    return image;
}

// A root (node 0) refined by two leaves (1 and 2); every node has a payload of a length that is
// not a multiple of 8, and a texture of its own when `textured`
Sample writeSample(const TempDir& dir, bool textured, const std::string& metadata = "{\"note\":\"sample\"}") {
    Sample s;
    s.textured = textured;
    VertexLayout layout;
    layout.normals = true;
    layout.texCoords = textured;
    Bm3dWriter writer(pathToUtf8(dir.file("sample.bm3d")), layout, {3, 4, textured ? 3u : 0u});

    const uint32_t tex[3] = {textured ? 0u : kNoTexture, textured ? 1u : kNoTexture, textured ? 2u : kNoTexture};
    const uint16_t vertices[3] = {6, 4, 5}, faces[3] = {4, 2, 3};
    const std::vector<std::vector<PatchRecord>> patches = {
        {{1, 2, tex[0]}, {2, 4, tex[0]}},
        {{kSink, 2, tex[1]}},
        {{kSink, 3, tex[2]}},
    };
    for (uint32_t n = 0; n < 3; n++) {
        NodeRecord node;
        node.error = float(3 - n);
        node.vertexCount = vertices[n];
        node.faceCount = faces[n];
        node.sphere.center[0] = float(n);
        node.sphere.radius = 2.0f;
        node.tightRadius = 1.0f;
        s.payloads.push_back(fakePayload(21 + 14 * n, uint8_t(0x50 + n)));
        writer.addNode(node, patches[n].data(), patches[n].size(), s.payloads.back().data(), s.payloads.back().size());
    }
    if (textured)
        for (uint32_t t = 0; t < 3; t++) {
            s.images.push_back(fakeImage(23 + 7 * t, uint8_t(0x60 + t)));
            writer.addTexture(s.images.back().data(), s.images.back().size());
        }
    SphereRecord sphere;
    sphere.radius = 3.0f;
    writer.finish(sphere, metadata);
    s.bytes = readFile(pathToUtf8(dir.file("sample.bm3d")));
    return s;
}

TEST(FormatWriter, aModelIsReadBackAsItWasWritten) {
    for (bool textured : {false, true}) {
        TempDir dir;
        const Sample s = writeSample(dir, textured);
        Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("sample.bm3d")));
        const HeaderRecord& h = reader.header();
        EXPECT_EQ(h.nodeCount, 4u);
        EXPECT_EQ(h.patchCount, 4u);
        EXPECT_EQ(h.textureCount, textured ? 3u : 0u);
        EXPECT_EQ(h.flags, textured ? 3u : 1u);
        EXPECT_EQ(h.vertexCount, 6u + 4 + 5);  // the sum of the nodes
        EXPECT_EQ(h.faceCount, 4u + 2 + 3);
        EXPECT_EQ(h.sphere.radius, 3.0f);
        EXPECT_EQ(h.indexOffset, 128u);
        EXPECT_EQ(h.metadataOffset % 8, 0u);
        EXPECT_EQ(h.metadataOffset + h.metadataLength, s.bytes.size());  // no byte after the metadata
        EXPECT_EQ(reader.rootCount(), 1u);
        EXPECT_FALSE(reader.isLeaf(0));
        EXPECT_TRUE(reader.isLeaf(1));
        EXPECT_TRUE(reader.isLeaf(2));
        EXPECT_EQ(reader.sink(), kSink);
        EXPECT_EQ(reader.patchBegin(0), 0u);
        EXPECT_EQ(reader.patchEnd(0), 2u);
        EXPECT_EQ(reader.patchEnd(2), 4u);

        // the sink: every field is 0 but firstPatch
        const NodeRecord& sink = reader.nodes()[kSink];
        EXPECT_EQ(sink.firstPatch, 4u);
        EXPECT_EQ(sink.payloadOffset, 0u);
        EXPECT_EQ(sink.vertexCount, 0);

        for (uint32_t n = 0; n < 3; n++) {
            const NodeRecord& node = reader.nodes()[n];
            EXPECT_EQ(node.payloadOffset % 8, 0u) << n;
            EXPECT_EQ(node.payloadLength, s.payloads[n].size()) << n;  // the exact length, not the padded one
            EXPECT_EQ(node.error, float(3 - n));
            EXPECT_EQ(reader.readNodePayload(n), s.payloads[n]) << n;
            EXPECT_EQ(reader.nodeTexture(n), textured ? int64_t(n) : -1);
        }
        EXPECT_LT(reader.nodes()[0].payloadOffset, reader.nodes()[1].payloadOffset);
        for (uint32_t t = 0; t < h.textureCount; t++) {
            EXPECT_EQ(reader.textures()[t].offset % 8, 0u);
            EXPECT_EQ(reader.readTexture(t), s.images[t]) << t;
        }
        EXPECT_EQ(reader.metadata(), "{\"note\":\"sample\"}");
        EXPECT_EQ(reader.patches()[0].node, 1u);
        EXPECT_EQ(reader.patches()[0].triangleOffset, 2u);
    }
}

TEST(FormatWriter, everyGapBetweenTheItemsIsZeroAndTheItemsFollowEachOtherAtMultiplesOfEight) {
    TempDir dir;
    const Sample s = writeSample(dir, true);
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("sample.bm3d")));
    std::vector<std::pair<uint64_t, uint64_t>> items;  // offset, length
    for (uint32_t n = 0; n < 3; n++)
        items.push_back({reader.nodes()[n].payloadOffset, reader.nodes()[n].payloadLength});
    for (const TextureRecord& t : reader.textures())
        items.push_back({t.offset, t.length});
    uint64_t at = reader.header().indexOffset + reader.header().indexLength;
    for (const auto& [offset, length] : items) {
        EXPECT_EQ(offset, at);  // each starts where the previous one's padding ends
        for (uint64_t i = offset + length; i < alignUp(offset + length); i++)
            EXPECT_EQ(s.bytes[i], 0) << i;
        at = alignUp(offset + length);
    }
    EXPECT_EQ(reader.header().metadataOffset, at);
    // the padding of the patch table, between the patches and the texture table
    const IndexLayout where = indexLayout(4, 4, 3);
    for (uint64_t i = where.patchTable + 4 * kPatchRecordSize; i < where.textureTable; i++)
        EXPECT_EQ(s.bytes[128 + i], 0);
}

// --- the writer refuses what the format does not hold -------------------------------------------

TEST(FormatWriter, aNodeWithNoTriangleAfterEncodingIsAnErrorAndNoFileIsWritten) {
    TempDir dir;
    {
        VertexLayout layout;
        Bm3dWriter writer(pathToUtf8(dir.file("o.bm3d")), layout, {1, 1, 0});
        NodeRecord node;
        node.vertexCount = 0;  // the encoder dropped every triangle and every vertex
        node.faceCount = 0;
        PatchRecord patch;
        patch.node = 1;
        Bytes stream(10, 1);
        try {
            writer.addNode(node, &patch, 1, stream.data(), stream.size());
            FAIL() << "a node without triangle was accepted";
        } catch (const Error& e) {
            EXPECT_NE(std::string(e.what()).find("no triangle"), std::string::npos) << e.what();
        }
    }
    EXPECT_TRUE(dir.list().empty());  // not even the file under its temporary name
}

TEST(FormatWriter, theTrianglesOfTheNodeMustEndWhereItsLastPatchEnds) {
    TempDir dir;
    VertexLayout layout;
    Bm3dWriter writer(pathToUtf8(dir.file("o.bm3d")), layout, {1, 1, 0});
    NodeRecord node;
    node.vertexCount = 3;
    node.faceCount = 2;
    PatchRecord patch;
    patch.node = 1;
    patch.triangleOffset = 1;
    Bytes stream(10, 1);
    EXPECT_THROW(writer.addNode(node, &patch, 1, stream.data(), stream.size()), Error);
}

TEST(FormatWriter, aModelWithTextureCoordinatesHasTexturesAndOneWithoutDoesNot) {
    TempDir dir;
    VertexLayout layout;
    layout.texCoords = true;
    EXPECT_THROW(Bm3dWriter(pathToUtf8(dir.file("a.bm3d")), layout, {1, 1, 0}), Error);
    layout.texCoords = false;
    EXPECT_THROW(Bm3dWriter(pathToUtf8(dir.file("b.bm3d")), layout, {1, 1, 1}), Error);
    EXPECT_TRUE(dir.list().empty());
}

TEST(FormatWriter, aModelThatIsNotCompleteIsNotWritten) {
    TempDir dir;
    VertexLayout layout;
    {
        Bm3dWriter writer(pathToUtf8(dir.file("o.bm3d")), layout, {2, 2, 0});
        NodeRecord node;
        node.vertexCount = 3;
        node.faceCount = 1;
        PatchRecord patch;
        patch.node = 2;
        patch.triangleOffset = 1;
        Bytes stream(10, 1);
        writer.addNode(node, &patch, 1, stream.data(), stream.size());
        EXPECT_THROW(writer.finish(SphereRecord(), "{}"), Error);  // one node of two
    }
    EXPECT_TRUE(dir.list().empty());
}

TEST(FormatWriter, aFileThatIsNotFinishedLeavesNothingAndAnEarlierFileIsKept) {
    TempDir dir;
    writeExample(dir);
    const Bytes before = readFile(pathToUtf8(dir.file("quad.bm3d")));
    {
        VertexLayout layout;
        Bm3dWriter writer(pathToUtf8(dir.file("quad.bm3d")), layout, {1, 1, 0});
        EXPECT_EQ(dir.list(), (std::vector<std::string>{"quad.bm3d", "quad.bm3d.part"}));
    }
    EXPECT_EQ(dir.list(), std::vector<std::string>{"quad.bm3d"});
    EXPECT_EQ(readFile(pathToUtf8(dir.file("quad.bm3d"))), before);
}

TEST(FormatWriter, aFileIsGivenItsNameOnlyWhenItIsComplete) {
    TempDir dir;
    VertexLayout layout;
    Bm3dWriter writer(pathToUtf8(dir.file("o.bm3d")), layout, {1, 1, 0});
    NodeRecord node;
    node.vertexCount = 3;
    node.faceCount = 1;
    PatchRecord patch;
    patch.node = 1;
    patch.triangleOffset = 1;
    Bytes stream(10, 1);
    writer.addNode(node, &patch, 1, stream.data(), stream.size());
    EXPECT_EQ(dir.list(), std::vector<std::string>{"o.bm3d.part"});
    writer.finish(SphereRecord(), "");
    EXPECT_EQ(dir.list(), std::vector<std::string>{"o.bm3d"});
}

// --- the metadata block -------------------------------------------------------------------------

TEST(FormatMetadata, noBlockIsTheEmptyObject) {
    TempDir dir;
    const Sample s = writeSample(dir, false, "");
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("sample.bm3d")));
    EXPECT_EQ(reader.header().metadataLength, 0u);
    EXPECT_EQ(reader.header().metadataOffset, s.bytes.size());
    EXPECT_EQ(reader.metadata(), "{}");
}

std::string objectOfLength(size_t length) {
    // {"k":"aaa...a"} is 8 bytes more than the a's
    return "{\"k\":\"" + std::string(length - 8, 'a') + "\"}";
}

TEST(FormatMetadata, aBlockOfOneMebibyteIsWrittenAndReadAndOneByteMoreIsRefused) {
    TempDir dir;
    const Sample s = writeSample(dir, false, objectOfLength(1048576));
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("sample.bm3d")));
    EXPECT_EQ(reader.header().metadataLength, 1048576u);
    EXPECT_EQ(reader.metadata().size(), 1048576u);

    TempDir other;
    try {
        writeSample(other, false, objectOfLength(1048577));
        FAIL() << "a block over 1 MiB was written";
    } catch (const MetadataTooLargeError& e) {
        EXPECT_NE(std::string(e.what()).find("1048577"), std::string::npos) << e.what();  // the message gives the length
    }
    EXPECT_TRUE(other.list().empty());
}

TEST(FormatMetadata, theWriterRefusesABlockThatIsNotAJsonObject) {
    for (const char* bad : {"[1,2]", "null", "{\"a\":}", "{\"a\":1} x", "\xEF\xBB\xBF{}"}) {
        TempDir dir;
        EXPECT_THROW(writeSample(dir, false, bad), Error) << bad;
        EXPECT_TRUE(dir.list().empty()) << bad;
    }
}

TEST(FormatMetadata, aReaderMayRefuseABlockOverOneMebibyteAndReportsABadBlockWhenItIsAsked) {
    TempDir dir;
    Bytes b = writeSample(dir, false).bytes;
    // a block of 1 MiB + 1 appended by hand: the file opens, the metadata is refused
    const uint64_t metadataOffset = [&] { uint64_t v; std::memcpy(&v, &b[64], 8); return v; }();
    b.resize(metadataOffset);
    const std::string block = objectOfLength(1048577);
    b.insert(b.end(), block.begin(), block.end());
    const uint64_t length = block.size();
    std::memcpy(&b[72], &length, 8);  // the tests run on little-endian machines, as the format does
    Bm3dReader reader = Bm3dReader::fromBytes(b);
    try {
        reader.metadata();
        FAIL();
    } catch (const FormatError& e) {
        EXPECT_EQ(e.kind(), FormatErrorKind::BadMetadata);
    }
}

TEST(FormatMetadata, aBlockThatIsNotAJsonObjectIsReportedWhenItIsAskedForAndTheFileStaysUsable) {
    TempDir dir;
    const Bytes good = writeSample(dir, false, "{\"a\":1}").bytes;
    const uint64_t metadataOffset = [&] { uint64_t v; std::memcpy(&v, &good[64], 8); return v; }();
    for (const std::string& bad : std::vector<std::string>{std::string("[1]"), std::string("not json"), std::string("{\"a\":"), std::string("\xEF\xBB\xBF{}"),
                                  std::string("{\"a\":\"\xFF\"}"), std::string("{\"a\":1}\0", 8)}) {
        Bytes b(good.begin(), good.begin() + std::ptrdiff_t(metadataOffset));
        b.insert(b.end(), bad.begin(), bad.end());
        const uint64_t length = bad.size();
        std::memcpy(&b[72], &length, 8);
        Bm3dReader reader = Bm3dReader::fromBytes(b);  // opens
        EXPECT_EQ(reader.readNodePayload(0).size(), 21u);
        try {
            reader.metadata();
            FAIL() << bad;
        } catch (const FormatError& e) {
            EXPECT_EQ(e.kind(), FormatErrorKind::BadMetadata) << bad;
        }
    }
}

// --- opening a file: every check, in the order of the specification ------------------------------

constexpr size_t kIndex = 128;
size_t nodeAt(uint32_t n) { return kIndex + 56 * n; }
// the patch table of the sample (four nodes with the sink)
size_t patchAt(uint32_t p) { return kIndex + 56 * 4 + 12 * p; }
size_t textureAt(uint32_t t) { return kIndex + indexLayout(4, 4, 3).textureTable + 16 * t; }

void put16(Bytes& b, size_t at, uint16_t v) { std::memcpy(&b[at], &v, 2); }
void put32(Bytes& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
void put64(Bytes& b, size_t at, uint64_t v) { std::memcpy(&b[at], &v, 8); }
uint64_t get64(const Bytes& b, size_t at) { uint64_t v; std::memcpy(&v, &b[at], 8); return v; }
// the bits of floats that are not finite
constexpr uint32_t kNan = 0x7fc00000u;
constexpr uint32_t kInfinity = 0x7f800000u;
constexpr uint32_t kMinusInfinity = 0xff800000u;

// Expects the file to be refused with this error and a message that holds `fragment`
void expectRefused(const Bytes& file, FormatErrorKind kind, const std::string& fragment, const std::string& what) {
    try {
        Bm3dReader::fromBytes(file);
        ADD_FAILURE() << what << ": the file was opened";
    } catch (const FormatError& e) {
        EXPECT_EQ(int(e.kind()), int(kind)) << what << ": " << e.what();
        EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos) << what << ": " << e.what();
    }
}

struct Case {
    std::string what;
    FormatErrorKind kind;
    std::string fragment;
    std::function<void(Bytes&)> change;
};

void expectCases(const Bytes& file, const std::vector<Case>& cases) {
    for (const Case& c : cases) {
        Bytes bad = file;
        c.change(bad);
        expectRefused(bad, c.kind, c.fragment, c.what);
    }
}

TEST(FormatReader, theSampleIsValid) {
    TempDir dir;
    for (bool textured : {false, true}) {
        const Sample s = writeSample(dir, textured);
        EXPECT_NO_THROW(Bm3dReader::fromBytes(s.bytes));
    }
}

TEST(FormatReader, aFileThatCannotBeReadIsAnIoError) {
    TempDir dir;
    try {
        Bm3dReader::open(pathToUtf8(dir.file("absent.bm3d")));
        FAIL();
    } catch (const FormatError& e) {
        EXPECT_EQ(e.kind(), FormatErrorKind::Io);
        EXPECT_NE(std::string(e.what()).find("absent.bm3d"), std::string::npos) << e.what();
    }
}

TEST(FormatReader, theHeaderIsChecked) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    expectCases(file, {
        {"fewer than 128 bytes", FormatErrorKind::TruncatedHeader, "127", [](Bytes& b) { b.resize(127); }},
        {"an empty file", FormatErrorKind::TruncatedHeader, "0 bytes", [](Bytes& b) { b.clear(); }},
        {"a wrong magic", FormatErrorKind::BadMagic, "BM3D", [](Bytes& b) { b[3] = 'X'; }},
        {"the magic of the Nexus container", FormatErrorKind::BadMagic, "Nexus", [](Bytes& b) {
             const Bytes nexus = fromHex("20 73 78 4e");
             std::copy(nexus.begin(), nexus.end(), b.begin());
         }},
        {"version 0", FormatErrorKind::UnsupportedVersion, "version 0", [](Bytes& b) { put32(b, 4, 0); }},
        {"version 2", FormatErrorKind::UnsupportedVersion, "version 2 and this reader reads up to version 1", [](Bytes& b) { put32(b, 4, 2); }},
        {"version 4294967295", FormatErrorKind::UnsupportedVersion, "4294967295", [](Bytes& b) { put32(b, 4, 0xFFFFFFFFu); }},
        {"a header smaller than 128", FormatErrorKind::BadHeaderSize, "120", [](Bytes& b) { put32(b, 8, 120); }},
        {"a header size that is not a multiple of 8", FormatErrorKind::BadHeaderSize, "132", [](Bytes& b) { put32(b, 8, 132); }},
        {"a header larger than the file", FormatErrorKind::BadHeaderSize, "no more than", [](Bytes& b) { put32(b, 8, uint32_t(b.size() + 8)); }},
        {"no CORTO_COMPRESSED flag", FormatErrorKind::CompressionFlagMissing, "corto", [](Bytes& b) { put32(b, 12, 2); }},
        {"no flag at all", FormatErrorKind::CompressionFlagMissing, "corto", [](Bytes& b) { put32(b, 12, 0); }},
        {"an unknown flag", FormatErrorKind::UnknownFlags, "0x00000007", [](Bytes& b) { put32(b, 12, 7); }},
        {"the highest flag", FormatErrorKind::UnknownFlags, "0x80000003", [](Bytes& b) { put32(b, 12, 0x80000003u); }},
        {"textures and no WebP flag", FormatErrorKind::UnknownFlags, "disagree", [](Bytes& b) { put32(b, 12, 1); }},
        {"the first byte of a layout", FormatErrorKind::UnsupportedLayout, "layout", [](Bytes& b) { b[16] = 8; }},
        {"a colour stored as BYTE", FormatErrorKind::UnsupportedLayout, "layout", [](Bytes& b) { b[16 + 4] = 1; b[16 + 5] = 4; }},
        {"a fifth vertex attribute", FormatErrorKind::UnsupportedLayout, "layout", [](Bytes& b) { b[16 + 8] = 7; b[16 + 9] = 1; }},
        {"a face attribute", FormatErrorKind::UnsupportedLayout, "layout", [](Bytes& b) { b[32 + 2] = 7; b[32 + 3] = 1; }},
        {"32-bit indexes", FormatErrorKind::UnsupportedLayout, "layout", [](Bytes& b) { b[32] = 6; }},
        {"no texture coordinates in a model with textures", FormatErrorKind::UnsupportedLayout, "texture coordinates", [](Bytes& b) { b[16 + 6] = 0; b[16 + 7] = 0; }},
        {"fewer than 2 nodes", FormatErrorKind::BadCounts, "1 nodes", [](Bytes& b) { put32(b, 96, 1); }},
        {"no node", FormatErrorKind::BadCounts, "0 nodes", [](Bytes& b) { put32(b, 96, 0); }},
        {"a centre of the sphere that is not a number", FormatErrorKind::BadSphere, "not finite", [](Bytes& b) { put32(b, 112, kNan); }},
        {"an infinite centre of the sphere", FormatErrorKind::BadSphere, "not finite", [](Bytes& b) { put32(b, 108, kMinusInfinity); }},
        {"a radius that is not a number", FormatErrorKind::BadSphere, "radius nan", [](Bytes& b) { put32(b, 120, kNan); }},
        {"an infinite radius", FormatErrorKind::BadSphere, "radius inf", [](Bytes& b) { put32(b, 120, kInfinity); }},
    });

    // a file with no textures: the layout must not have the coordinates, and the WebP flag is clear
    const Bytes plain = writeSample(dir, false).bytes;
    expectCases(plain, {
        {"texture coordinates and no texture", FormatErrorKind::UnsupportedLayout, "texture coordinates",
         [](Bytes& b) { b[16 + 6] = 7; b[16 + 7] = 2; }},
        {"the WebP flag and no texture", FormatErrorKind::UnknownFlags, "disagree", [](Bytes& b) { put32(b, 12, 3); }},
    });
}

TEST(FormatReader, theChecksAreMadeInTheOrderOfTheSpecification) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    // two faults: the first of the table is the one reported
    Bytes b = file;
    put32(b, 4, 2);       // version
    b[3] = 'X';           // magic
    put32(b, 8, 1);       // header size
    expectRefused(b, FormatErrorKind::BadMagic, "BM3D", "magic before version");
    b[3] = 'D';
    expectRefused(b, FormatErrorKind::UnsupportedVersion, "version 2", "version before header size");
    put32(b, 4, 1);
    put32(b, 12, 0);      // flags
    expectRefused(b, FormatErrorKind::BadHeaderSize, "1", "header size before flags");
    put32(b, 8, 128);
    put32(b, 96, 1);      // counts
    b[16] = 9;            // layout
    put32(b, 12, 0x10);   // unknown flag...
    expectRefused(b, FormatErrorKind::CompressionFlagMissing, "corto", "compression flag before unknown flags");
    put32(b, 12, 0x11);
    expectRefused(b, FormatErrorKind::UnknownFlags, "0x00000011", "unknown flags before layout");
    put32(b, 12, 3);
    expectRefused(b, FormatErrorKind::UnsupportedLayout, "layout", "layout before counts");
    b[16] = 7;
    put32(b, 116, kNan);  // the sphere
    expectRefused(b, FormatErrorKind::BadCounts, "nodes", "counts before the sphere");
    put32(b, 96, 4);
    put64(b, 80, 129);    // index offset
    expectRefused(b, FormatErrorKind::BadSphere, "sphere", "the sphere before the ranges");
    std::copy(file.begin() + 116, file.begin() + 120, b.begin() + 116);
    put64(b, 80, 129);    // index offset
    put64(b, 64, 1);      // metadata offset
    expectRefused(b, FormatErrorKind::OutOfRange, "index", "index before metadata");
}

TEST(FormatReader, theRangesOfTheHeaderAreChecked) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    const uint64_t indexLength = get64(file, 88);
    const uint64_t metadataOffset = get64(file, 64);
    const uint64_t size = file.size();
    expectCases(file, {
        {"an index that does not start at a multiple of 8", FormatErrorKind::OutOfRange, "index starts at 129", [](Bytes& b) { put64(b, 80, 129); }},
        {"an index inside the header", FormatErrorKind::OutOfRange, "index starts at 120", [](Bytes& b) { put64(b, 80, 120); }},
        {"an index that is not the length the counts need", FormatErrorKind::OutOfRange, "counts need", [indexLength](Bytes& b) { put64(b, 88, indexLength + 8); }},
        {"an index that is shorter", FormatErrorKind::OutOfRange, "counts need", [indexLength](Bytes& b) { put64(b, 88, indexLength - 8); }},
        {"an index beyond the end of the file", FormatErrorKind::OutOfRange, "not inside the file", [size](Bytes& b) { put64(b, 80, size / 8 * 8 - 8); }},
        {"an index offset that would wrap", FormatErrorKind::OutOfRange, "not inside the file", [](Bytes& b) { put64(b, 80, 0xFFFFFFFFFFFFFFF8ull); }},
        {"a metadata block that does not start at a multiple of 8", FormatErrorKind::OutOfRange, "metadata starts at", [metadataOffset](Bytes& b) { put64(b, 64, metadataOffset + 1); }},
        {"a metadata block inside the index", FormatErrorKind::OutOfRange, "metadata starts at", [](Bytes& b) { put64(b, 64, 128); }},
        {"a metadata block that ends after the file", FormatErrorKind::OutOfRange, "truncated", [](Bytes& b) { put64(b, 72, get64(b, 72) + 1); }},
        {"a metadata length that would wrap", FormatErrorKind::OutOfRange, "truncated", [](Bytes& b) { put64(b, 72, 0xFFFFFFFFFFFFFFFFull); }},
        {"a file cut short", FormatErrorKind::OutOfRange, "truncated", [](Bytes& b) { b.pop_back(); }},
        {"a file cut inside the payloads", FormatErrorKind::OutOfRange, "truncated", [metadataOffset](Bytes& b) { b.resize(size_t(metadataOffset) - 8); }},
        {"a file cut after the index", FormatErrorKind::OutOfRange, "truncated", [indexLength](Bytes& b) { b.resize(size_t(128 + indexLength)); }},
    });
}

TEST(FormatReader, theRangeOfEveryPayloadAndTextureIsChecked) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    const uint64_t dataStart = 128 + indexLayout(4, 4, 3).length;
    const uint64_t metadataOffset = get64(file, 64);
    expectCases(file, {
        {"a payload that does not start at a multiple of 8", FormatErrorKind::OutOfRange, "payload of node 1 starts at",
         [](Bytes& b) { put64(b, nodeAt(1), get64(b, nodeAt(1)) + 1); }},
        {"an empty payload", FormatErrorKind::OutOfRange, "payload of node 2 is empty", [](Bytes& b) { put64(b, nodeAt(2) + 8, 0); }},
        {"a payload before the data", FormatErrorKind::OutOfRange, "payload of node 0", [dataStart](Bytes& b) { put64(b, nodeAt(0), dataStart - 8); }},
        {"a payload that ends in the metadata", FormatErrorKind::OutOfRange, "payload of node 2",
         [metadataOffset](Bytes& b) { put64(b, nodeAt(2) + 8, metadataOffset); }},
        {"a payload that starts after the metadata", FormatErrorKind::OutOfRange, "payload of node 1",
         [metadataOffset](Bytes& b) { put64(b, nodeAt(1), metadataOffset + 8); }},
        {"a payload whose end would wrap", FormatErrorKind::OutOfRange, "payload of node 1",
         [](Bytes& b) { put64(b, nodeAt(1) + 8, 0xFFFFFFFFFFFFFFF0ull); }},
        {"a texture that does not start at a multiple of 8", FormatErrorKind::OutOfRange, "texture 1 starts at",
         [](Bytes& b) { put64(b, textureAt(1), get64(b, textureAt(1)) + 4); }},
        {"an empty texture", FormatErrorKind::OutOfRange, "texture 2 is empty", [](Bytes& b) { put64(b, textureAt(2) + 8, 0); }},
        {"a texture beyond the data", FormatErrorKind::OutOfRange, "texture 0",
         [metadataOffset](Bytes& b) { put64(b, textureAt(0) + 8, metadataOffset); }},
        {"a texture whose end would wrap", FormatErrorKind::OutOfRange, "texture 0",
         [](Bytes& b) { put64(b, textureAt(0), 0xFFFFFFFFFFFFFFF8ull); }},
    });
}

TEST(FormatReader, twoSectionsThatOverlapAreRefused) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    expectCases(file, {
        {"two payloads at the same place", FormatErrorKind::OverlappingSections, "overlap",
         [](Bytes& b) { put64(b, nodeAt(1), get64(b, nodeAt(0))); }},
        {"a payload that starts inside the one before", FormatErrorKind::OverlappingSections, "overlap",
         [](Bytes& b) { put64(b, nodeAt(1), get64(b, nodeAt(0)) + 8); }},
        {"a payload that runs into the next", FormatErrorKind::OverlappingSections, "payload of node 0",
         [](Bytes& b) { put64(b, nodeAt(0) + 8, get64(b, nodeAt(0) + 8) + 8); }},
        {"a texture inside a payload", FormatErrorKind::OverlappingSections, "texture 0",
         [](Bytes& b) { put64(b, textureAt(0), get64(b, nodeAt(2))); }},
        {"two textures at the same place", FormatErrorKind::OverlappingSections, "texture",
         [](Bytes& b) { put64(b, textureAt(2), get64(b, textureAt(1))); }},
        {"a payload on a texture", FormatErrorKind::OverlappingSections, "payload of node 0",
         [](Bytes& b) { put64(b, nodeAt(0), get64(b, textureAt(1))); }},
    });
}

TEST(FormatReader, aFileWithNoMetadataBlockIsValid) {
    TempDir dir;
    const Sample s = writeSample(dir, false, "");
    EXPECT_NO_THROW(Bm3dReader::fromBytes(s.bytes));
}

TEST(FormatReader, bytesAfterTheMetadataBlockAreNotAnError) {
    TempDir dir;
    Bytes b = writeSample(dir, true).bytes;
    b.insert(b.end(), {1, 2, 3});
    EXPECT_NO_THROW(Bm3dReader::fromBytes(b));
}

TEST(FormatReader, theIndexIsChecked) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    expectCases(file, {
        {"the first node does not start at patch 0", FormatErrorKind::BadIndex, "first patch of node 0",
         [](Bytes& b) { put32(b, nodeAt(0) + 52, 1); }},
        {"a first patch that decreases", FormatErrorKind::BadIndex, "decreases",
         [](Bytes& b) { put32(b, nodeAt(2) + 52, 1); }},
        {"a sink that does not end the patches", FormatErrorKind::BadIndex, "first patch of the sink",
         [](Bytes& b) { put32(b, nodeAt(3) + 52, 5); }},
        {"a sink with a payload", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put64(b, nodeAt(3), 8); }},
        {"a sink with a length", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put64(b, nodeAt(3) + 8, 8); }},
        {"a sink with an error", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put32(b, nodeAt(3) + 16, 0x3f800000); }},
        {"a sink with vertices", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put16(b, nodeAt(3) + 20, 3); }},
        {"a sink with faces", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put16(b, nodeAt(3) + 22, 1); }},
        {"a sink with a cone", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put16(b, nodeAt(3) + 30, 1); }},
        {"a sink with a sphere centre", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put32(b, nodeAt(3) + 36, 0x3f800000); }},
        {"a sink with a radius", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put32(b, nodeAt(3) + 44, 0x3f800000); }},
        {"a sink with a tight radius", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put32(b, nodeAt(3) + 48, 0x3f800000); }},
        {"a real node with no patch", FormatErrorKind::BadIndex, "node 2 has no patch",
         [](Bytes& b) { put32(b, nodeAt(2) + 52, 4); }},
        {"a node without triangles", FormatErrorKind::BadIndex, "node 1 has", [](Bytes& b) { put16(b, nodeAt(1) + 22, 0); }},
        {"a node of two vertices", FormatErrorKind::BadIndex, "node 1 has 2 vertices", [](Bytes& b) { put16(b, nodeAt(1) + 20, 2); }},
        {"an error that is not a number", FormatErrorKind::BadIndex, "node 1 has a value that is not finite: error nan",
         [](Bytes& b) { put32(b, nodeAt(1) + 16, kNan); }},
        {"an infinite error", FormatErrorKind::BadIndex, "error inf", [](Bytes& b) { put32(b, nodeAt(0) + 16, kInfinity); }},
        {"an infinite centre", FormatErrorKind::BadIndex, "node 2 has a value that is not finite",
         [](Bytes& b) { put32(b, nodeAt(2) + 32, kMinusInfinity); }},
        {"a radius that is not a number", FormatErrorKind::BadIndex, "radius nan", [](Bytes& b) { put32(b, nodeAt(0) + 44, kNan); }},
        {"a tight radius that is not a number", FormatErrorKind::BadIndex, "tight radius nan",
         [](Bytes& b) { put32(b, nodeAt(2) + 48, kNan); }},
        {"a sink with an error of minus zero", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put32(b, nodeAt(3) + 16, 0x80000000u); }},
        {"a sink with a radius that is not a number", FormatErrorKind::BadIndex, "sink", [](Bytes& b) { put32(b, nodeAt(3) + 44, kNan); }},
        {"a patch that names a node beyond the table", FormatErrorKind::BadIndex, "names node 9",
         [](Bytes& b) { put32(b, patchAt(0), 9); }},
        {"a patch that names its own node", FormatErrorKind::BadIndex, "not stored after it",
         [](Bytes& b) { put32(b, patchAt(2), 1); }},
        {"a patch that names a node before its own", FormatErrorKind::BadIndex, "not stored after it",
         [](Bytes& b) { put32(b, patchAt(3), 0); }},
        {"a node with a patch to the sink and one to a node", FormatErrorKind::BadIndex, "name the sink and patches that do not",
         [](Bytes& b) { put32(b, patchAt(1), kSink); }},
        {"triangle offsets that decrease", FormatErrorKind::BadIndex, "below the one before it",
         [](Bytes& b) { put32(b, patchAt(0) + 4, 5); }},
        {"a last triangle offset that is not the face count", FormatErrorKind::BadIndex, "last triangle offset of node 1",
         [](Bytes& b) { put32(b, patchAt(2) + 4, 1); }},
        {"a texture beyond the table", FormatErrorKind::BadIndex, "names texture 3",
         [](Bytes& b) { put32(b, patchAt(2) + 8, 3); }},
        {"patches with different textures", FormatErrorKind::BadIndex, "different textures",
         [](Bytes& b) { put32(b, patchAt(1) + 8, 2); }},
        {"a node with no texture in a model with textures", FormatErrorKind::BadIndex, "has no texture",
         [](Bytes& b) { put32(b, patchAt(3) + 8, kNoTexture); }},
    });
}

TEST(FormatReader, theRulesOfTheIndexAreCheckedInTheOrderOfTheSpecification) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    Bytes b = file;
    put32(b, patchAt(2) + 8, 9);                // a texture out of the table (8)
    put32(b, patchAt(3) + 4, 1);                // a last triangle offset that is wrong (7)
    put32(b, patchAt(0), kSink);                // some patches to the sink and some not (6)
    put32(b, patchAt(1), 0);                    // a node before it (5)
    put16(b, nodeAt(1) + 22, 0);                // no triangles (4)
    put32(b, nodeAt(2) + 52, 4);                // no patch (3)
    put32(b, nodeAt(3) + 22, 1);                // a sink with a field (2)
    put32(b, nodeAt(3) + 52, 9);                // the sink's first patch (1)
    expectRefused(b, FormatErrorKind::BadIndex, "first patch of the sink", "1");
    put32(b, nodeAt(3) + 52, 4);
    expectRefused(b, FormatErrorKind::BadIndex, "sink (node 3)", "2");
    put16(b, nodeAt(3) + 22, 0);
    expectRefused(b, FormatErrorKind::BadIndex, "node 2 has no patch", "3");
    put32(b, nodeAt(2) + 52, 3);
    expectRefused(b, FormatErrorKind::BadIndex, "node 1 has", "4");
    put16(b, nodeAt(1) + 22, 2);
    expectRefused(b, FormatErrorKind::BadIndex, "not stored after it", "5");
    put32(b, patchAt(1), 2);
    expectRefused(b, FormatErrorKind::BadIndex, "name the sink and patches that do not", "6");
    put32(b, patchAt(0), 1);
    expectRefused(b, FormatErrorKind::BadIndex, "last triangle offset of node 2", "7");
    put32(b, patchAt(3) + 4, 3);
    expectRefused(b, FormatErrorKind::BadIndex, "names texture 9", "8");
    put32(b, patchAt(2) + 8, 2);
    EXPECT_NO_THROW(Bm3dReader::fromBytes(b));
}

// Each rule over the whole table before the next: a later node that breaks an earlier rule is the
// one reported, not an earlier node that breaks a later rule
TEST(FormatReader, eachRuleOfTheIndexIsCheckedOverTheWholeTableBeforeTheNext) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    Bytes b = file;
    put32(b, patchAt(1) + 8, 2);       // node 0: patches with different textures (14)
    put32(b, patchAt(3), 0);           // node 2: a patch that names a node before it (9)
    put32(b, nodeAt(2) + 16, kNan);    // node 2: an error that is not a number (7)
    put32(b, patchAt(0) + 4, 9);       // node 0: a triangle offset above the next one (11)
    expectRefused(b, FormatErrorKind::BadIndex, "node 2 has a value that is not finite", "7 before 9, 11 and 14");
    std::copy(file.begin() + nodeAt(2) + 16, file.begin() + nodeAt(2) + 20, b.begin() + nodeAt(2) + 16);
    expectRefused(b, FormatErrorKind::BadIndex, "patch 3 of node 2 names node 0", "9 before 11 and 14");
    put32(b, patchAt(3), kSink);
    expectRefused(b, FormatErrorKind::BadIndex, "patch 1 of node 0", "11 before 14");
    std::copy(file.begin() + patchAt(0) + 4, file.begin() + patchAt(0) + 8, b.begin() + patchAt(0) + 4);
    put32(b, patchAt(3) + 8, 9);       // node 2: a texture beyond the table (13)
    expectRefused(b, FormatErrorKind::BadIndex, "patch 3 of node 2 names texture 9", "13 before 14");
}

// A model with two roots: nodes 0 and 1 are refined by nodes 2 and 3
TEST(FormatReader, theRootsAreTheNodesBeforeTheSmallestNodeARootLinksTo) {
    TempDir dir;
    VertexLayout layout;
    Bm3dWriter writer(pathToUtf8(dir.file("o.bm3d")), layout, {4, 4, 0});
    const uint32_t sink = 4;
    const uint32_t link[4] = {2, 3, sink, sink};
    for (uint32_t n = 0; n < 4; n++) {
        NodeRecord node;
        node.vertexCount = 3;
        node.faceCount = 1;
        PatchRecord patch;
        patch.node = link[n];
        patch.triangleOffset = 1;
        Bytes stream(8, 1);
        writer.addNode(node, &patch, 1, stream.data(), stream.size());
    }
    writer.finish(SphereRecord(), "");
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("o.bm3d")));
    EXPECT_EQ(reader.rootCount(), 2u);
    EXPECT_FALSE(reader.isLeaf(0));
    EXPECT_TRUE(reader.isLeaf(3));
}

TEST(FormatReader, aTextureMustBeAWebpContainerWhenItIsRead) {
    TempDir dir;
    const Bytes file = writeSample(dir, true).bytes;
    Bm3dReader good = Bm3dReader::fromBytes(file);
    EXPECT_NO_THROW(good.readTexture(2));
    const uint64_t at = get64(file, textureAt(1));
    for (size_t damage : {size_t(0), size_t(3), size_t(8), size_t(11)}) {
        Bytes b = file;
        b[size_t(at) + damage] ^= 0xFF;
        Bm3dReader reader = Bm3dReader::fromBytes(b);  // the file still opens
        try {
            reader.readTexture(1);
            FAIL() << damage;
        } catch (const FormatError& e) {
            EXPECT_EQ(e.kind(), FormatErrorKind::CorruptTexture) << damage;
        }
        EXPECT_NO_THROW(reader.readTexture(0));
    }
    // too short for a container
    Bytes b = file;
    put64(b, textureAt(1) + 8, 11);
    Bm3dReader shortTexture = Bm3dReader::fromBytes(b);
    EXPECT_THROW(shortTexture.readTexture(1), FormatError);
}

TEST(FormatReader, aPayloadThatIsNotACortoStreamIsACorruptNodeWhenItIsDecoded) {
    TempDir dir;
    Bm3dReader reader = Bm3dReader::fromBytes(writeSample(dir, false).bytes);  // opens: the payloads are not looked at
    try {
        reader.decodeNode(0);
        FAIL();
    } catch (const FormatError& e) {
        EXPECT_EQ(e.kind(), FormatErrorKind::CorruptNode);
    }
    EXPECT_THROW(reader.readNodePayload(3), Error);  // the sink is not a node
}

// --- content identity ------------------------------------------------------------------------------

// The reference computation of the specification, from the bytes of a file in memory
std::string specHash(const Bytes& f) {
    const uint64_t metadataOffset = get64(f, 64), metadataLength = get64(f, 72);
    Sha256 h;
    h.update(f.data(), 72);
    const uint8_t zeros[8] = {};
    h.update(zeros, 8);
    h.update(f.data() + 80, size_t(metadataOffset - 80));
    h.update(f.data() + metadataOffset + metadataLength, size_t(f.size() - (metadataOffset + metadataLength)));
    return h.finishHex();
}

TEST(FormatContentHash, itIsTheHashOfTheSpecification) {
    TempDir dir;
    for (bool textured : {false, true}) {
        const Sample s = writeSample(dir, textured);
        Bm3dReader reader = Bm3dReader::fromBytes(s.bytes);
        EXPECT_EQ(reader.contentHash(), specHash(s.bytes));
        EXPECT_EQ(reader.contentHash().size(), 64u);
        Bm3dReader onDisk = Bm3dReader::open(pathToUtf8(dir.file("sample.bm3d")));
        EXPECT_EQ(onDisk.contentHash(), reader.contentHash());
    }
}

TEST(FormatContentHash, aFileLongerThanTheReadingBufferIsHashedWhole) {
    // payloads over a mebibyte make the hash read the file in several pieces
    TempDir dir;
    VertexLayout layout;
    Bm3dWriter writer(pathToUtf8(dir.file("big.bm3d")), layout, {1, 1, 0});
    NodeRecord node;
    node.vertexCount = 3;
    node.faceCount = 1;
    PatchRecord patch;
    patch.node = 1;
    patch.triangleOffset = 1;
    Bytes stream(2500000);
    for (size_t i = 0; i < stream.size(); i++)
        stream[i] = uint8_t(i * 31 + i / 7);
    writer.addNode(node, &patch, 1, stream.data(), stream.size());
    writer.finish(SphereRecord(), "{\"a\":1}");
    const Bytes file = readFile(pathToUtf8(dir.file("big.bm3d")));
    Bm3dReader reader = Bm3dReader::open(pathToUtf8(dir.file("big.bm3d")));
    EXPECT_EQ(reader.contentHash(), specHash(file));
}

TEST(FormatContentHash, onlyTheMetadataChangesWhenOnlyTheMetadataChanges) {
    TempDir a, b;
    const Sample first = writeSample(a, true, "{\"generator\":\"bm3d 1.0.0\"}");
    const Sample second = writeSample(b, true, "{\"generator\":\"bm3d 1.1.0\",\"crs\":\"EPSG:32633\",\"x\":[1,2,3,4,5,6,7,8,9]}");
    ASSERT_NE(first.bytes, second.bytes);
    ASSERT_NE(first.bytes.size(), second.bytes.size());
    EXPECT_EQ(Bm3dReader::fromBytes(first.bytes).contentHash(), Bm3dReader::fromBytes(second.bytes).contentHash());
    // the other bytes are the same but for the length of the block (bytes 72 to 79)
    EXPECT_EQ(slice(first.bytes, 0, 72), slice(second.bytes, 0, 72));
    EXPECT_EQ(slice(first.bytes, 80, 600), slice(second.bytes, 80, 600));
    EXPECT_NE(slice(first.bytes, 72, 8), slice(second.bytes, 72, 8));
}

// How a tool replaces the metadata of a finished file: cut it at the block, append the new one,
// write the new length
TEST(FormatContentHash, replacingTheMetadataOfAFinishedFileKeepsTheHashAndTheOtherSections) {
    TempDir dir;
    const Sample s = writeSample(dir, true);
    Bytes b = s.bytes;
    const std::string replacement = "{\"crs\":\"EPSG:4326\",\"origin\":[1,2,3]}";
    b.resize(size_t(get64(b, 64)));
    b.insert(b.end(), replacement.begin(), replacement.end());
    put64(b, 72, replacement.size());
    Bm3dReader reader = Bm3dReader::fromBytes(b);
    EXPECT_EQ(reader.metadata(), replacement);
    EXPECT_EQ(reader.contentHash(), Bm3dReader::fromBytes(s.bytes).contentHash());
    EXPECT_EQ(reader.readNodePayload(2), s.payloads[2]);
    EXPECT_EQ(reader.readTexture(2), s.images[2]);
}

TEST(FormatContentHash, aByteOutsideTheMetadataChangesIt) {
    TempDir dir;
    const Sample s = writeSample(dir, true);
    const std::string reference = Bm3dReader::fromBytes(s.bytes).contentHash();
    Bm3dReader parsed = Bm3dReader::fromBytes(s.bytes);
    const uint64_t metadataOffset = parsed.header().metadataOffset;
    const uint64_t padding = parsed.nodes()[0].payloadOffset + parsed.nodes()[0].payloadLength;  // a padding byte after the first payload
    ASSERT_LT(padding, parsed.nodes()[1].payloadOffset);

    // every byte of the file but the ones of the metadata block and the length field
    std::vector<size_t> positions = {0, 3, 4, 8, 12, 16, 32, 48, 56, 80, 88, 96, 104, 108, 124, 127, 128, 200, 300,
                                     size_t(parsed.nodes()[0].payloadOffset), size_t(parsed.nodes()[0].payloadOffset + 5),
                                     size_t(padding), size_t(parsed.nodes()[2].payloadOffset),
                                     size_t(parsed.textures()[0].offset), size_t(parsed.textures()[2].offset + 20)};
    for (size_t at : positions) {
        Bytes b = s.bytes;
        b[at] ^= 0x01;
        // some of these flips make a file the reader refuses; the hash is the one of the bytes,
        // so it is computed as the specification says, not through the reader
        EXPECT_NE(specHash(b), reference) << at;
    }
    // the same, through the reader, for the bytes that leave the file valid
    for (size_t at : {size_t(padding), size_t(parsed.nodes()[0].payloadOffset + 5), size_t(parsed.textures()[1].offset + 3),
                      size_t(124), size_t(127)}) {
        Bytes b = s.bytes;
        b[at] ^= 0x01;
        EXPECT_NE(Bm3dReader::fromBytes(b).contentHash(), reference) << at;
    }
    // a byte of the metadata block, and the length field with a block that really has that length, do not
    for (uint64_t at = metadataOffset; at < s.bytes.size(); at++) {
        Bytes b = s.bytes;
        b[size_t(at)] = 'x';
        EXPECT_EQ(specHash(b), reference) << at;
    }
}

TEST(FormatContentHash, theBytesAfterTheMetadataBlockAreHashed) {
    TempDir dir;
    Bytes b = writeSample(dir, false).bytes;
    const std::string before = Bm3dReader::fromBytes(b).contentHash();
    b.push_back(7);
    const std::string after = Bm3dReader::fromBytes(b).contentHash();
    EXPECT_NE(before, after);
    EXPECT_EQ(after, specHash(b));
    b.back() = 8;
    EXPECT_NE(Bm3dReader::fromBytes(b).contentHash(), after);
}

}  // namespace
