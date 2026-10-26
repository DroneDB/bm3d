// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "outcome.h"

#include <array>
#include <system_error>

#include "model_files.h"

namespace bm3dtest {

Outcome runOn(const TempDir& dir, const fs::path& input, std::chrono::seconds timeout) {
    Outcome outcome;
    outcome.outputFolder = dir.file("out");
    std::error_code ec;
    fs::remove_all(outcome.outputFolder, ec);
    fs::create_directories(outcome.outputFolder);
    outcome.output = outcome.outputFolder / "o.bm3d";
    outcome.process = runProcess(bm3dExecutable(), {pathToUtf8(input), pathToUtf8(outcome.output)}, timeout);
    return outcome;
}

bool isResourceFailure(const std::string& text) {
    // std::bad_alloc; the messages of the scratch files (src/common/mappedfile.cpp, scratchfile.cpp,
    // virtualarray.cpp); the vertex cache of the OBJ loader; the errors of the system
    static const std::array<const char*, 9> wordings = {
        "bad_alloc",
        "unable to create",
        "could not resize file",
        "could not write the scratch file",
        "could not read the scratch file",
        "virtual memory error",
        "cache_plyvertex",
        "No space left on device",
        "Cannot allocate memory"};
    for (const char* wording : wordings)
        if (text.find(wording) != std::string::npos)
            return true;
    return false;
}

std::string whatIsWrong(const Outcome& outcome) {
    const ProcessResult& p = outcome.process;
    if (p.timedOut)
        return "it did not end in time";
    if (p.exitCode < 0 || p.exitCode > 2)
        return "it ended with the code " + std::to_string(p.exitCode) + " (a crash, or a signal if negative)\n" + p.err;
    // A sanitizer that does not stop the program (UndefinedBehaviorSanitizer by default) only prints
    for (const char* word : {"runtime error", "Sanitizer", "Assertion", "terminate called"})
        if (p.err.find(word) != std::string::npos)
            return std::string("its stderr holds \"") + word + "\":\n" + p.err;

    std::vector<std::string> left;
    for (const auto& entry : fs::directory_iterator(outcome.outputFolder))
        left.push_back(pathToUtf8(entry.path().filename()));
    if (p.exitCode == 0) {
        if (left.size() != 1 || !fs::exists(outcome.output))
            return "it exited 0 and the output folder holds " + std::to_string(left.size()) + " files, not the output";
        if (readBinaryFile(outcome.output).substr(0, 4) != "BM3D")
            return "it exited 0 and the output does not start with the magic number BM3D";
        return "";
    }
    if (!left.empty())
        return "it exited " + std::to_string(p.exitCode) + " and left " + left.front() + " in the output folder";
    if (p.exitCode == 1 && !isResourceFailure(p.err))
        return "it exited 1 (the build failed) and the message does not name memory or a scratch file:\n" + p.err;
    if (p.exitCode == 2 && p.err.empty())
        return "it exited 2 and said nothing";
    return "";
}

}  // namespace bm3dtest
