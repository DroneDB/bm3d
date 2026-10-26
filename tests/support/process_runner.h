// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "paths.h"

namespace bm3dtest {

struct ProcessResult {
    // The exit code; minus the signal number when the process was killed by a signal (also by
    // the time limit); -1 when it could not be started
    int exitCode = -1;
    // What it wrote to stdout and to stderr, with "\n" line ends on every platform
    std::string out;
    std::string err;
    bool timedOut = false;

    // Both streams, for a message that says what the program printed
    std::string output() const { return out + err; }
};

// Runs `executable` with `arguments` (UTF-8, none of them parsed by a shell) and waits for it,
// with stdin closed and stdout and stderr captured apart. A process still running after
// `timeout` is killed, so that a hang fails a test instead of the whole run. The environment is
// that of the caller (see EnvVarGuard).
ProcessResult runProcess(const fs::path& executable,
                         const std::vector<std::string>& arguments,
                         std::chrono::seconds timeout = std::chrono::seconds(180));

}  // namespace bm3dtest
