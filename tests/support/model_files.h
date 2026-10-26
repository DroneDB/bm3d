// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small models written as text or bytes by the tests, one mesh format at a time.

#pragma once

#include <array>
#include <cstring>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "paths.h"

namespace bm3dtest {

void writeTextFile(const fs::path& file, const std::string& text);
void writeBinaryFile(const fs::path& file, const std::string& bytes);
// The bytes of a file; an empty string for one that is not there
std::string readBinaryFile(const fs::path& file);

// The smallest model the build accepts: one triangle
constexpr const char* kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

// A flat cells x cells grid of squares, two triangles each, covered once by the texture
// `texture` (a file in `folder`): writes grid.obj and grid.mtl
void writeGridModel(const fs::path& folder, int cells, const std::string& texture);

// Two triangles over a square with texture coordinates, and the material that names the texture
// file (in `folder`): writes model.obj and model.mtl. `uv` replaces the four "vt" lines.
void writeTexturedModel(const fs::path& folder, const std::string& texture, const char* uv = nullptr);

// The same square surface as a PLY header with `faces` faces, and the four vertices
std::string plyHeader(int faces);

// A binary little-endian PLY of the square as two triangles; the face list counts with
// `countType` and indexes with `indexType`, each one of uchar, ushort, int, uint
template <typename Count, typename Index>
void writeBinaryPly(const fs::path& file, const char* countType, const char* indexType);

// A triangle with the coordinates of its three corners
using Triangle = std::array<std::array<float, 3>, 3>;

// The triangles of a n x n grid of squares over a wave (so that no triangle is flat on
// another), two triangles per square
std::vector<Triangle> waveTriangles(int n);

std::string asciiStl(const std::vector<Triangle>& triangles);
// A binary STL: the 80 byte header (`header` padded with spaces or cut) and, after each triangle, its
// 16 bit attribute (`attributes[i]`, 0 where there is none)
std::string binaryStl(const std::vector<Triangle>& triangles,
                      const std::string& header = "binary wave stl",  // does not start with "solid"
                      const std::vector<uint16_t>& attributes = {});

// The same triangles as an OBJ and as an ASCII PLY, one vertex per corner
std::string objOf(const std::vector<Triangle>& triangles);
std::string plyOf(const std::vector<Triangle>& triangles);
// The same as a binary little-endian PLY (float coordinates, a uchar count and int indices)
std::string binaryPlyOf(const std::vector<Triangle>& triangles);

// --- the template

namespace detail {
template <typename T>
void putLittleEndian(std::string& s, T v) {
    for (size_t i = 0; i < sizeof(T); i++)
        s.push_back(static_cast<char>((static_cast<uint64_t>(v) >> (8 * i)) & 0xff));
}
}  // namespace detail

template <typename Count, typename Index>
void writeBinaryPly(const fs::path& file, const char* countType, const char* indexType) {
    std::string s = std::string("ply\nformat binary_little_endian 1.0\nelement vertex 4\n"
                                "property float x\nproperty float y\nproperty float z\n"
                                "element face 2\nproperty list ") +
                    countType + " " + indexType + " vertex_indices\nend_header\n";
    const float xyz[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    for (const auto& v : xyz)
        for (float c : v) {
            uint32_t bits;
            static_assert(sizeof(bits) == sizeof(c), "a float is 32 bits");
            std::memcpy(&bits, &c, 4);
            detail::putLittleEndian(s, bits);
        }
    const int faces[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (const auto& f : faces) {
        detail::putLittleEndian(s, static_cast<Count>(3));
        for (int i : f)
            detail::putLittleEndian(s, static_cast<Index>(i));
    }
    writeBinaryFile(file, s);
}

}  // namespace bm3dtest
