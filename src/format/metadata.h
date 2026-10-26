// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The metadata block of a .bm3d file (docs/FORMAT.md, section 7): one JSON object with what the
// builder says about itself and the input, and the members a caller adds.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json_object.h"

namespace bm3d::detail {

// One setting of the build, as a number
struct BuildOption {
    std::string name;
    bool isReal = false;
    int64_t integer = 0;
    float real = 0;

    static BuildOption whole(std::string name, int64_t value) {
        BuildOption o;
        o.name = std::move(name);
        o.integer = value;
        return o;
    }
    static BuildOption fractional(std::string name, float value) {
        BuildOption o;
        o.name = std::move(name);
        o.isReal = true;
        o.real = value;
        return o;
    }
};

// What the builder read of the input
struct SourceInfo {
    std::string name;  // the file name without its folder, UTF-8
    uint64_t vertices = 0;
    uint64_t faces = 0;
    float boundsMin[3] = {0, 0, 0};
    float boundsMax[3] = {0, 0, 0};
};

// What is wrong with a metadata block, or an empty string: it is one JSON object in UTF-8 without a
// byte order mark (section 7). The writer and the reader both ask.
std::string metadataBlockError(std::string_view block);

// Reads the JSON object a caller wants stored (--metadata) into its members. Returns what is
// wrong with it, or an empty string when it is an object whose top-level names are neither
// reserved (generator, buildOptions, source, anything starting with "bm3d") nor repeated. The
// problem names the key.
std::string parseExtraMetadata(std::string_view text, std::vector<JsonMember>& members);

// The block: generator, buildOptions and source, then the extra members as they were supplied.
// Compact, no white space. Throws Error when `extras` holds a reserved or a repeated name.
std::string composeMetadata(const std::string& generator, const std::vector<BuildOption>& options,
                            const SourceInfo& source, const std::vector<JsonMember>& extras);

}  // namespace bm3d::detail
