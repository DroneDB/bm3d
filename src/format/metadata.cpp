// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadata.h"

#include <charconv>
#include <set>

#include "../common/error.h"

namespace bm3d::detail {

namespace {

bool isReserved(const std::string& name) {
    return name == "generator" || name == "buildOptions" || name == "source" || name.compare(0, 4, "bm3d") == 0;
}

// The first problem with the names of the members, or an empty string
std::string extraMembersError(const std::vector<JsonMember>& members) {
    std::set<std::string> seen;
    for (const JsonMember& member : members) {
        if (isReserved(member.name))
            return "the metadata has the key \"" + member.name + "\", which is reserved (generator, buildOptions, source "
                   "and the names that start with bm3d are written by bm3d)";
        if (!seen.insert(member.name).second)
            return "the metadata has the key \"" + member.name + "\" twice";
    }
    return {};
}

// A JSON string literal. The text is UTF-8: a byte that does not belong to a valid sequence (a
// file name of the machine that is not UTF-8) becomes U+FFFD, so that the block stays valid.
std::string quote(const std::string& text) {
    static const char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += static_cast<char>(c);
            } else if (c < 0x20) {
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += static_cast<char>(c);
            }
            i++;
            continue;
        }
        // the length of the sequence the lead byte announces, and whether the rest is valid
        size_t length = 0;
        if (c >= 0xC2 && c <= 0xDF)
            length = 2;
        else if (c >= 0xE0 && c <= 0xEF)
            length = 3;
        else if (c >= 0xF0 && c <= 0xF4)
            length = 4;
        bool valid = length > 0 && i + length <= text.size();
        uint32_t code = length == 2 ? c & 0x1Fu : length == 3 ? c & 0x0Fu : c & 0x07u;
        for (size_t k = 1; valid && k < length; k++) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            valid = (next & 0xC0u) == 0x80u;
            code = (code << 6) | (next & 0x3Fu);
        }
        if (valid) {
            const uint32_t smallest = length == 2 ? 0x80u : length == 3 ? 0x800u : 0x10000u;
            valid = code >= smallest && code <= 0x10FFFFu && !(code >= 0xD800u && code <= 0xDFFFu);
        }
        if (valid) {
            out.append(text, i, length);
            i += length;
        } else {
            out += "\xEF\xBF\xBD";
            i++;
        }
    }
    return out + '"';
}

std::string number(float value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

std::string number(uint64_t value) {
    return std::to_string(value);
}

std::string triple(const float (&v)[3]) {
    return "[" + number(v[0]) + "," + number(v[1]) + "," + number(v[2]) + "]";
}

}  // namespace

std::string metadataBlockError(std::string_view block) {
    if (block.substr(0, 3) == "\xEF\xBB\xBF")
        return "the block begins with a byte order mark";
    const std::string problem = jsonObjectError(block);
    return problem.empty() ? problem : "the block is not a JSON object: " + problem;
}

std::string parseExtraMetadata(std::string_view text, std::vector<JsonMember>& members) {
    const std::string problem = jsonObjectMembers(text, members);
    if (!problem.empty())
        return "the metadata is not a JSON object: " + problem;
    const std::string names = extraMembersError(members);
    if (!names.empty())
        members.clear();
    return names;
}

std::string composeMetadata(const std::string& generator, const std::vector<BuildOption>& options,
                            const SourceInfo& source, const std::vector<JsonMember>& extras) {
    const std::string problem = extraMembersError(extras);
    if (!problem.empty())
        throw Error(problem);

    std::string out = "{\"generator\":" + quote(generator) + ",\"buildOptions\":{";
    for (size_t i = 0; i < options.size(); i++) {
        const BuildOption& o = options[i];
        out += (i ? "," : "") + quote(o.name) + ":" + (o.isReal ? number(o.real) : std::to_string(o.integer));
    }
    out += "},\"source\":{\"name\":" + quote(source.name) + ",\"vertices\":" + number(source.vertices) +
           ",\"faces\":" + number(source.faces) + ",\"bounds\":{\"min\":" + triple(source.boundsMin) +
           ",\"max\":" + triple(source.boundsMax) + "}}";
    for (const JsonMember& member : extras)
        out += "," + member.rawName + ":" + member.rawValue;
    return out + "}";
}

}  // namespace bm3d::detail
