// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "version_file.h"

#include <fstream>

namespace bm3dtest {

std::string versionFileText() {
    std::ifstream in(std::string(BM3D_TEST_SOURCE_DIR) + "/VERSION");
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

}  // namespace bm3dtest
