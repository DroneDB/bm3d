// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the program may do with an input that is not what it expects, and how a test tells it: the
// program is run as a process on one file, and the outcome is judged by one rule for the fixed cases
// and for the mutation loop alike.

#pragma once

#include <chrono>
#include <string>

#include "cli_support.h"
#include "temp_dir.h"

namespace bm3dtest {

// One run of the program on `input`, writing to a folder of its own that is empty before it
struct Outcome {
    ProcessResult process;
    fs::path outputFolder;
    fs::path output;
};

// Runs the program on `input` (and removes what an earlier run left in the folder `outputFolder`
// of `dir`, which is created if need be). The time limit makes a program that does not end a
// failure of the test, not of the whole run.
Outcome runOn(const TempDir& dir, const fs::path& input,
              std::chrono::seconds timeout = std::chrono::seconds(20));

// Whether the stderr of the run is a failure of the machine: memory that ran out or a scratch file
// that could not be created, grown or mapped (the wordings of the program and of the system for
// those). That is the only reason for the exit code 1 on an input that is not a model.
bool isResourceFailure(const std::string& stderrText);

// The rule for any input: the program ends in time and does not crash; it exits 0 and writes a file
// with the magic of the container, or exits 2 (invalid input) or 1 for want of memory or scratch
// space, and writes nothing; and neither a sanitizer nor an assertion has anything to say. Returns
// what is wrong, or an empty string.
std::string whatIsWrong(const Outcome& outcome);

}  // namespace bm3dtest
