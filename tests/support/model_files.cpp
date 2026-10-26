// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "model_files.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace bm3dtest {

void writeBinaryFile(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out)
        throw std::runtime_error("cannot write " + pathToUtf8(file));
}

std::string readBinaryFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeTextFile(const fs::path& file, const std::string& text) {
    writeBinaryFile(file, text);
}

void writeGridModel(const fs::path& folder, int cells, const std::string& texture) {
    std::ostringstream obj;
    obj << "mtllib grid.mtl\n";
    for (int y = 0; y <= cells; y++)
        for (int x = 0; x <= cells; x++)
            obj << "v " << x << " " << y << " 0\n";
    for (int y = 0; y <= cells; y++)
        for (int x = 0; x <= cells; x++)
            obj << "vt " << double(x) / cells << " " << double(y) / cells << "\n";
    obj << "usemtl m\n";
    auto index = [cells](int x, int y) { return y * (cells + 1) + x + 1; };
    for (int y = 0; y < cells; y++)
        for (int x = 0; x < cells; x++) {
            const int a = index(x, y), b = index(x + 1, y), c = index(x + 1, y + 1), d = index(x, y + 1);
            obj << "f " << a << "/" << a << " " << b << "/" << b << " " << c << "/" << c << "\n";
            obj << "f " << a << "/" << a << " " << c << "/" << c << " " << d << "/" << d << "\n";
        }
    writeTextFile(folder / "grid.obj", obj.str());
    writeTextFile(folder / "grid.mtl", "newmtl m\nKd 1 1 1\nmap_Kd " + texture + "\n");
}

void writeTexturedModel(const fs::path& folder, const std::string& texture, const char* uv) {
    writeTextFile(folder / "model.obj",
                  std::string("mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n") +
                      (uv != nullptr ? uv : "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n") +
                      "usemtl m\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\n");
    writeTextFile(folder / "model.mtl", "newmtl m\nKd 1 1 1\nmap_Kd " + texture + "\n");
}

std::string plyHeader(int faces) {
    std::ostringstream s;
    s << "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
      << "element face " << faces << "\nproperty list uchar int vertex_indices\nend_header\n"
      << "0 0 0\n1 0 0\n1 1 0\n0 1 0\n";
    return s.str();
}

std::vector<Triangle> waveTriangles(int n) {
    auto corner = [n](int x, int y) {
        const float u = float(x) / float(n), v = float(y) / float(n);
        return std::array<float, 3>{u, v, 0.1f * std::sin(6.0f * u) * std::cos(5.0f * v)};
    };
    std::vector<Triangle> triangles;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            triangles.push_back({corner(x, y), corner(x + 1, y), corner(x + 1, y + 1)});
            triangles.push_back({corner(x, y), corner(x + 1, y + 1), corner(x, y + 1)});
        }
    return triangles;
}

std::string asciiStl(const std::vector<Triangle>& triangles) {
    std::ostringstream s;
    s << std::setprecision(9) << "solid wave\n";
    for (const Triangle& t : triangles) {
        s << "  facet normal 0 0 1\n    outer loop\n";
        for (const auto& c : t)
            s << "      vertex " << c[0] << " " << c[1] << " " << c[2] << "\n";
        s << "    endloop\n  endfacet\n";
    }
    s << "endsolid wave\n";
    return s.str();
}

std::string binaryStl(const std::vector<Triangle>& triangles) {
    std::string s = "binary wave stl";  // a header that does not start with "solid"
    s.resize(80, ' ');
    detail::putLittleEndian(s, static_cast<uint32_t>(triangles.size()));
    auto putFloat = [&s](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        detail::putLittleEndian(s, bits);
    };
    for (const Triangle& t : triangles) {
        for (const float n : {0.0f, 0.0f, 1.0f})
            putFloat(n);
        for (const auto& c : t)
            for (const float v : c)
                putFloat(v);
        detail::putLittleEndian(s, static_cast<uint16_t>(0));
    }
    return s;
}

std::string objOf(const std::vector<Triangle>& triangles) {
    std::ostringstream s;
    s << std::setprecision(9);
    for (const Triangle& t : triangles)
        for (const auto& c : t)
            s << "v " << c[0] << " " << c[1] << " " << c[2] << "\n";
    for (size_t i = 0; i < triangles.size(); i++)
        s << "f " << 3 * i + 1 << " " << 3 * i + 2 << " " << 3 * i + 3 << "\n";
    return s.str();
}

std::string plyOf(const std::vector<Triangle>& triangles) {
    std::ostringstream s;
    s << std::setprecision(9) << "ply\nformat ascii 1.0\nelement vertex " << 3 * triangles.size()
      << "\nproperty float x\nproperty float y\nproperty float z\nelement face " << triangles.size()
      << "\nproperty list uchar int vertex_indices\nend_header\n";
    for (const Triangle& t : triangles)
        for (const auto& c : t)
            s << c[0] << " " << c[1] << " " << c[2] << "\n";
    for (size_t i = 0; i < triangles.size(); i++)
        s << "3 " << 3 * i << " " << 3 * i + 1 << " " << 3 * i + 2 << "\n";
    return s.str();
}

std::string binaryPlyOf(const std::vector<Triangle>& triangles) {
    std::ostringstream header;
    header << "ply\nformat binary_little_endian 1.0\nelement vertex " << 3 * triangles.size()
           << "\nproperty float x\nproperty float y\nproperty float z\nelement face " << triangles.size()
           << "\nproperty list uchar int vertex_indices\nend_header\n";
    std::string s = header.str();
    for (const Triangle& t : triangles)
        for (const auto& c : t)
            for (const float v : c) {
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                detail::putLittleEndian(s, bits);
            }
    for (size_t i = 0; i < triangles.size(); i++) {
        detail::putLittleEndian(s, static_cast<uint8_t>(3));
        for (size_t k = 0; k < 3; k++)
            detail::putLittleEndian(s, static_cast<int32_t>(3 * i + k));
    }
    return s;
}

}  // namespace bm3dtest
