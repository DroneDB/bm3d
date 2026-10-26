// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace bm3d {

// Checks that `text` is one JSON object as RFC 8259 defines it (a leading UTF-8 byte order mark
// is accepted, which some Windows editors write): strict syntax, strings and escapes valid, the
// text valid UTF-8, no unpaired surrogate escape, nesting at most kJsonMaxDepth deep, nothing
// but white space after the object. Returns an empty string when it is, otherwise what is wrong
// and the byte offset where it was found. It does not build a document; it is what --metadata
// needs before the content is stored.
std::string jsonObjectError(std::string_view text);

// A member of the top-level object, as it stands in the text
struct JsonMember {
    std::string name;      // the name with its escapes resolved, in UTF-8
    std::string rawName;   // the name as written, quotes included
    std::string rawValue;  // the value as written, without the white space around it
};

// The same check, and the members of the top-level object in their order (duplicate names
// included: telling them apart is the caller's business). `members` is empty when the text is
// not a valid object.
std::string jsonObjectMembers(std::string_view text, std::vector<JsonMember>& members);

constexpr int kJsonMaxDepth = 256;

}  // namespace bm3d
