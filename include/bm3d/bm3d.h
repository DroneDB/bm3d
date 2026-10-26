// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// bm3d: builds a multiresolution, streamable 3D model from an OBJ, PLY or STL mesh.

#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace bm3d {

// What went wrong, in the terms a caller acts on.
enum class ErrorKind {
    // The input, the mesh or a texture its materials name, is missing, unreadable or cannot be
    // parsed. Fix or replace the file.
    InvalidInput,
    // An argument other than the input is invalid: a BuildOptions value out of range, an empty
    // output path.
    InvalidArgument,
    // The input was read and the build failed after that (memory, disk, a model the format
    // cannot hold, a failure writing the output), or memory or scratch space ran out while the
    // input was read. Nothing is left at the output path.
    BuildFailed,
};

// The one exception build() throws. what() is a UTF-8 message.
class Error : public std::runtime_error {
public:
    Error(ErrorKind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}

    ErrorKind kind() const noexcept { return kind_; }

private:
    ErrorKind kind_;
};

struct BuildOptions {
    // The threads a build may keep busy, 1 to 4096; 0 means one per core.
    int threads = 0;
};

// Builds the model in `input` (.obj, .ply or .stl, with the textures its materials name) into
// the file `output`. The output is corto-compressed and is written under a temporary name in
// its folder, then renamed: it appears only when the build succeeded, and an earlier file of
// that name is left alone when it fails. Throws Error.
void build(const std::filesystem::path& input,
           const std::filesystem::path& output,
           const BuildOptions& options = {});

// The version of this library, as in the VERSION file at the root of the sources.
const char* version() noexcept;

// The version of the file format build() writes. 0 is the Nexus container.
int formatVersion() noexcept;

}  // namespace bm3d
