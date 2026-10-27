// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// bm3d: builds a multiresolution, streamable 3D model from an OBJ, PLY or STL mesh.

#pragma once

#include <filesystem>
#include <functional>
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
    // A JSON object (UTF-8 text) whose members are stored in the metadata block of the file,
    // after the ones bm3d writes, as they are given. Empty: none. An object that is not valid, or
    // that has the top-level key generator, buildOptions or source, a key that starts with bm3d,
    // or the same key twice, is refused before anything is built (ErrorKind::InvalidArgument), as
    // is one that makes the block longer than 1 MiB.
    std::string metadata;
    // Called with the text of each warning (UTF-8, no prefix and no line end) as the build goes:
    // the input is built in a way that differs from the usual (textures left out, smaller nodes,
    // bytes of the input that are ignored). It is called on the thread that called build().
    // Empty: the warnings are dropped. The library writes nothing to the standard streams.
    std::function<void(const std::string& message)> logger;
};

// Builds the model in `input` (.obj, .ply or .stl, with the textures its materials name) into
// the file `output`, a .bm3d file of the version formatVersion() returns (docs/FORMAT.md). The
// output is written under a temporary name in its folder, then renamed: it appears only when the
// build succeeded, and an earlier file of that name is left alone when it fails. Throws Error.
void build(const std::filesystem::path& input,
           const std::filesystem::path& output,
           const BuildOptions& options = {});

// What is wrong with a text that is to be BuildOptions::metadata, or an empty string when
// build() accepts it: the same checks that build() makes first, so that a caller can refuse the
// text before it starts anything. The message names the key or the length.
std::string metadataError(const std::string& json);

// The version of this library, as in the VERSION file at the root of the sources.
const char* version() noexcept;

// The version of the file format build() writes: 1 (0 is the container of Nexus, which the first
// releases of bm3d wrote).
int formatVersion() noexcept;

}  // namespace bm3d
