// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace bm3d {

// Checks that `text` is one JSON object as RFC 8259 defines it (a leading UTF-8 byte order mark
// is accepted, which some Windows editors write): strict syntax, strings and escapes valid, the
// text valid UTF-8, no unpaired surrogate escape, nesting at most kJsonMaxDepth deep, nothing
// but white space after the object. Returns an empty string when it is, otherwise what is wrong
// and the byte offset where it was found. It does not build a document; it is what --metadata
// needs before the content is stored.
std::string jsonObjectError(std::string_view text);

constexpr int kJsonMaxDepth = 256;

}  // namespace bm3d
