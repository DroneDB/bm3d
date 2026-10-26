// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 (FIPS 180-4), for the content hash of a .bm3d file. No library of the build provides
// it, and a hash is no reason to add one.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bm3d::detail {

class Sha256 {
public:
    Sha256();

    void update(const void* data, size_t length);
    // The digest of everything given to update(); the object is not usable after it
    std::array<uint8_t, 32> finish();
    // The same as 64 lower-case hexadecimal digits
    std::string finishHex();

private:
    void block(const uint8_t* bytes);

    uint32_t state_[8];
    uint8_t buffer_[64];
    size_t buffered_ = 0;
    uint64_t length_ = 0;  // bytes given so far
};

// The digest of one buffer, as 64 lower-case hexadecimal digits
std::string sha256Hex(const void* data, size_t length);

}  // namespace bm3d::detail
