// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include "process_runner.h"

namespace bm3dtest {

// The program under test: the path CMake gives to the target
inline fs::path bm3dExecutable() {
    return pathFromUtf8(BM3D_TEST_EXE);
}

// Runs the program with the arguments (UTF-8)
inline ProcessResult runBm3d(const std::vector<std::string>& arguments) {
    return runProcess(bm3dExecutable(), arguments);
}

}  // namespace bm3dtest
