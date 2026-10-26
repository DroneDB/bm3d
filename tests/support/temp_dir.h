// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "paths.h"

namespace bm3dtest {

// A new, empty folder under the system temporary folder, named after the running test, removed
// with everything in it when the object goes away.
class TempDir {
public:
    TempDir();
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

    // The path of `relative` (UTF-8, '/' separators) in the folder; nothing is created
    fs::path file(const std::string& relative) const;

    // Creates the folder `relative` and the ones above it, and returns its path
    fs::path makeDir(const std::string& relative) const;

    // The names of the entries of the folder `relative` ("" for the folder itself), sorted
    std::vector<std::string> list(const std::string& relative = "") const;

private:
    fs::path path_;
};

}  // namespace bm3dtest
