// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace bm3dtest {

// The first line of the VERSION file at the root of the sources, which the build takes the
// version from; empty if it cannot be read
std::string versionFileText();

}  // namespace bm3dtest
