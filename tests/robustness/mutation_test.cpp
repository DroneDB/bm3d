// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A seeded mutation loop: small models in every format the program reads (OBJ, PLY as text and as
// binary, STL as text and as binary; with a texture where the format has one), and a PNG texture of an
// OBJ (the decoder of the PNG is stb, written for pictures that are not trusted), are damaged by flipping
// bytes, cutting the file, inserting bytes and repeating a range, and the program is run on each. Whatever
// the damage, it must end in time, not crash, say nothing to a sanitizer, and either build a file (exit 0)
// or refuse the input (exit 2) and leave nothing behind; exit 1 only for memory or scratch space (see
// outcome.h). A failure names the run, its seed and what was done to the file, and keeps the file.
//
// Every run is a function of BM3D_MUTATION_SEED (default 1) and its index, so a failure repeats with the
// same two. BM3D_MUTATION_RUNS (default 500) is the number of runs, shared among the seeds in turn; the
// sanitizers workflow runs the default and the normal one fewer.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "env_var_guard.h"
#include "model_files.h"
#include "outcome.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

// SplitMix64: a generator that gives the same numbers on every platform and library, which
// std::mt19937 with the distributions of the standard library does not
class Random {
public:
    explicit Random(uint64_t seed) : state_(seed) {}

    uint64_t next() {
        uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    // 0 to n - 1 (n > 0)
    size_t below(size_t n) { return size_t(next() % n); }

private:
    uint64_t state_;
};

int environmentNumber(const std::string& name, int fallback) {
    const auto value = readEnvironmentVariable(name);
    if (!value || value->empty())
        return fallback;
    try {
        return std::stoi(*value);
    } catch (const std::exception&) {
        ADD_FAILURE() << name << " is not a number: " << *value;
        return fallback;
    }
}

// --- the seeds

struct Seed {
    std::string name;
    std::string extension;
    std::string bytes;
    bool textured;       // the model names seed.mtl, and the material t.png
    size_t textLength;   // the file is text up to here: all of it, or the header of a binary PLY
    bool texture;        // the damaged file is t.png, the texture of a model that is not damaged
};

// A PNG of 64 x 64 pixels, the picture of the PNG seed
std::string pngSeed() {
    TempDir dir;
    writePng(dir.file("seed.png"), makeRaster(64, 64, 3, [](int band, int x, int y) { return double((x * 4 + y * 3 + band * 40) & 0xff); }));
    return readBinaryFile(dir.file("seed.png"));
}

const std::vector<Seed>& seeds() {
    static const std::vector<Seed> all = [] {
        const auto triangles = waveTriangles(3);
        std::vector<Seed> list{
            {"obj", "obj", texturedObjOf(triangles, "seed.mtl"), true, std::string::npos, false},
            {"ply_ascii", "ply", texturedPlyOf(triangles, "t.png", false), true, std::string::npos, false},
            {"ply_binary", "ply", texturedPlyOf(triangles, "t.png", true), true, 0, false},
            {"stl_ascii", "stl", asciiStl(triangles), false, std::string::npos, false},
            {"stl_binary", "stl", binaryStl(triangles), false, 0, false},
            {"png_texture", "png", pngSeed(), false, 0, true},
        };
        for (Seed& seed : list)
            if (seed.name == "ply_binary")
                seed.textLength = seed.bytes.find("end_header\n") + 11;
        return list;
    }();
    return all;
}

// --- the mutations

// Characters that make a text file say something else: signs, exponents, separators, words of numbers
const std::string kInteresting = " \n\r\t-+.e0123456789/#nafi";

// Numbers that break the code that reads them: not numbers, the edges of the types, too long for any
const char* const kNumbers[] = {"nan",        "inf",          "-inf",       "0",          "-0",        "1e38",
                                "-1e38",      "3.5e38",       "1e-45",      "2147483647", "2147483648", "4294967295",
                                "4294967296", "-1",           "-2147483648", "65536",      "255",       "256",
                                "99999999999999999999",       "0x10",       "1e999",      "."};

// The same, as 32 bit words of a binary file: NaN, infinities, the largest float and integers
const uint32_t kWords[] = {0x7fc00000u, 0x7f800000u, 0xff800000u, 0xffffffffu, 0u,          0x7fffffffu,
                           0x80000000u, 0x00000100u, 0x7f7fffffu, 0x00000001u, 0x01000000u, 0x0000ffffu};

std::string hex(uint8_t byte) {
    std::ostringstream s;
    s << std::hex << std::setw(2) << std::setfill('0') << int(byte);
    return s.str();
}

// Where to damage: in the first 160 bytes (the header, the counts) a third of the time, anywhere else
size_t position(Random& random, size_t size) {
    return random.below(3) == 0 ? random.below(std::min<size_t>(size, 160)) : random.below(size);
}

bool isSeparator(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '/';
}

// One damage to `bytes` (the first `textLength` of which are text); says what it did
std::string mutateOnce(std::string& bytes, size_t textLength, Random& random) {
    std::ostringstream said;
    static const size_t weights[] = {0, 0, 0, 1, 2, 2, 3, 3, 4, 4, 4};  // flip 3, cut 1, insert 2, repeat 2, replace 3
    switch (bytes.empty() ? 2 : weights[random.below(sizeof(weights) / sizeof(weights[0]))]) {
        case 0: {  // flip: some bytes become other bytes
            const size_t count = 1 + random.below(8);
            said << "flip";
            for (size_t i = 0; i < count; i++) {
                const size_t at = position(random, bytes.size());
                const uint8_t was = uint8_t(bytes[at]);
                const uint8_t now = random.below(2) ? uint8_t(was ^ (1 + random.below(255)))
                                                    : uint8_t(kInteresting[random.below(kInteresting.size())]);
                bytes[at] = char(now);
                said << " " << at << ":" << hex(was) << ">" << hex(now);
            }
            break;
        }
        case 1: {  // truncate
            const size_t length = position(random, bytes.size());
            bytes.resize(length);
            said << "truncate to " << length;
            break;
        }
        case 2: {  // insert bytes
            const size_t at = bytes.empty() ? 0 : position(random, bytes.size() + 1);
            const size_t count = 1 + random.below(32);
            std::string inserted;
            for (size_t i = 0; i < count; i++)
                inserted.push_back(random.below(2) ? char(random.below(256)) : kInteresting[random.below(kInteresting.size())]);
            bytes.insert(at, inserted);
            said << "insert " << count << " bytes at " << at;
            break;
        }
        case 3: {  // repeat a range
            const size_t from = position(random, bytes.size());
            const size_t length = 1 + random.below(std::min<size_t>(64, bytes.size() - from));
            const size_t at = position(random, bytes.size() + 1);
            bytes.insert(at, bytes.substr(from, length));
            said << "repeat " << length << " bytes of " << from << " at " << at;
            break;
        }
        default: {  // replace a number: the word or token at a position by one that breaks readers
            const size_t at = position(random, bytes.size());
            if (at < textLength) {
                size_t begin = at, end = at;
                while (begin > 0 && !isSeparator(bytes[begin - 1]))
                    begin--;
                while (end < bytes.size() && !isSeparator(bytes[end]))
                    end++;
                const std::string number = kNumbers[random.below(sizeof(kNumbers) / sizeof(kNumbers[0]))];
                said << "replace " << begin << ".." << end << " by " << number;
                bytes.replace(begin, end - begin, number);
            } else {
                const uint32_t word = kWords[random.below(sizeof(kWords) / sizeof(kWords[0]))];
                said << "word " << std::hex << word << std::dec << " at " << at;
                for (size_t i = 0; i < 4 && at + i < bytes.size(); i++)
                    bytes[at + i] = char((word >> (8 * i)) & 0xff);
            }
            break;
        }
    }
    return said.str();
}

// One to three damages
std::string mutate(std::string& bytes, size_t textLength, Random& random) {
    std::string said;
    for (size_t i = 0, count = 1 + random.below(3); i < count; i++)
        said += (i ? "; " : "") + mutateOnce(bytes, textLength, random);
    return said;
}

// --- the loop

class MutationLoop : public ::testing::TestWithParam<size_t> {};

TEST_P(MutationLoop, theProgramEndsInTimeAndKeepsTheRulesWhateverTheDamage) {
    const Seed& seed = seeds()[GetParam()];
    const int runs = environmentNumber("BM3D_MUTATION_RUNS", 500);
    const int base = environmentNumber("BM3D_MUTATION_SEED", 1);

    TempDir dir;
    const fs::path in = dir.makeDir("in");
    if (seed.textured || seed.texture) {
        if (!seed.texture)
            writePng(in / "t.png", gradientRaster(3));
        writeTextFile(in / "seed.mtl", "newmtl m\nKd 1 1 1\nmap_Kd t.png\n");
    }
    // what is run is the model; what is damaged is the model, or its texture
    const fs::path input = in / (seed.texture ? std::string("m.obj") : "m." + seed.extension);
    const fs::path damaged = seed.texture ? in / "t.png" : input;
    if (seed.texture)
        writeTextFile(input, texturedObjOf(waveTriangles(3), "seed.mtl"));

    int failures = 0, built = 0, refused = 0, scratch = 0;
    // this seed takes every seeds().size()-th run
    for (int run = int(GetParam()); run < runs && failures < 5; run += int(seeds().size())) {
        Random random(uint64_t(base) * 1000003u + uint64_t(run));
        std::string bytes = seed.bytes;
        const std::string said = mutate(bytes, seed.textLength, random);
        writeBinaryFile(damaged, bytes);

        const Outcome outcome = runOn(dir, input);
        const std::string wrong = whatIsWrong(outcome);
        if (wrong.empty()) {
            (outcome.process.exitCode == 0 ? built : outcome.process.exitCode == 2 ? refused : scratch)++;
            continue;
        }
        failures++;
        const fs::path kept = fs::temp_directory_path() / ("bm3d-mutation-" + seed.name + "-" + std::to_string(run) + "." +
                                                          seed.extension);
        writeBinaryFile(kept, bytes);
        ADD_FAILURE() << "run " << run << " of " << runs << " on the " << seed.name << " seed (BM3D_MUTATION_SEED=" << base
                      << "): " << said << "\n" << wrong << "\nthe file is kept as " << pathToUtf8(kept);
    }
    RecordProperty("built", built);
    RecordProperty("refused", refused);
    RecordProperty("resource", scratch);
}

INSTANTIATE_TEST_SUITE_P(Seeds, MutationLoop, ::testing::Range(size_t(0), seeds().size()),
                         [](const ::testing::TestParamInfo<size_t>& info) { return seeds()[info.param].name; });

}  // namespace
