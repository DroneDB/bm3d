// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File names in the tests are UTF-8 text, as they are in the program; they become a
// std::filesystem::path (UTF-16 on Windows) only where a file is touched.

#pragma once

#include <filesystem>
#include <string>

namespace bm3dtest {

namespace fs = std::filesystem;

// u8path is deprecated only from C++20, where the char8_t constructor takes its place
inline fs::path pathFromUtf8(const std::string& utf8) {
    return fs::u8path(utf8);
}

inline std::string pathToUtf8(const fs::path& path) {
    const auto utf8 = path.generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}

}  // namespace bm3dtest
