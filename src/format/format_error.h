// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "../common/error.h"

namespace bm3d::detail {

// What is wrong with a file that is not a .bm3d file of a version this code reads. The first
// group is found when the file is opened, in this order (docs/FORMAT.md, section 11); the second
// when what it concerns is used.
enum class FormatErrorKind {
    Io,  // the file cannot be opened or read

    TruncatedHeader,
    BadMagic,
    UnsupportedVersion,
    BadHeaderSize,
    CompressionFlagMissing,
    UnknownFlags,
    UnsupportedLayout,
    BadCounts,
    BadSphere,
    OutOfRange,
    OverlappingSections,
    BadIndex,

    CorruptNode,
    CorruptTexture,
    BadMetadata,
};

// A file that Bm3dReader refuses; what() says what is wrong and the value found
class FormatError : public Error {
public:
    FormatError(FormatErrorKind kind, const std::string& message) : Error(message), kind_(kind) {}

    FormatErrorKind kind() const noexcept { return kind_; }

private:
    FormatErrorKind kind_;
};

// The metadata block would be longer than a file can hold (docs/FORMAT.md, section 7)
class MetadataTooLargeError : public Error {
public:
    explicit MetadataTooLargeError(const std::string& message) : Error(message) {}
};

}  // namespace bm3d::detail
