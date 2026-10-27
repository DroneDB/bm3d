// bm3d: builds a multiresolution 3D model from an OBJ, PLY or STL mesh.
//
//   bm3d [--threads N] [--metadata <file.json>] <in.obj|ply|stl> <out>
//   bm3d --version
//   bm3d --help
//
// This is the command line of the bm3d library (include/bm3d/bm3d.h). A program that needs the
// builder, DroneDB for one, runs it as a separate process, so the mesh and image code never
// shares an address space with the host.
//
// While the build runs, one line per heartbeat interval (15 s) is written to stdout, so a
// supervisor that watches the program's output can tell a long build from a hang.
//
// Paths are UTF-8 on every platform. On Windows the program reads its UTF-16 command line and
// converts it, since the narrow argv of main is in the ANSI code page and cannot hold a model
// under, say, a Cyrillic or CJK folder name.
//
// Exit codes:
//   0   success (or --version / --help)
//   1   the build failed after the input was read, or memory or scratch space ran out while it
//       was read: the message is on stderr
//   2   the input is invalid: the mesh or a texture its materials name is missing, unreadable or
//       not parseable; message on stderr
//   64  wrong command line (the sysexits.h EX_USAGE value): message and usage on stderr
//
// Copyright (C) 2026 Digipa
// Licensed under the GNU General Public License, version 3 (see LICENSE). It derives from
// ddb_nexus_main.cpp of libnexus (DroneDB), the command line of the Nexus builder, which is
// licensed under version 3 of the GPL only, as this file is.
// SPDX-License-Identifier: GPL-3.0-only

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "bm3d/bm3d.h"
#include "json_object.h"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitBuildFailed = 1;
constexpr int kExitInvalidInput = 2;
constexpr int kExitUsage = 64;

constexpr const char* kProgram = "bm3d";

// Well below any idle timeout a supervisor is likely to apply to a silent child.
constexpr std::chrono::milliseconds kHeartbeatInterval{15000};

// The tests of the program cannot wait 15 s for a second line. Not part of the interface and
// not in --help.
constexpr const char* kHeartbeatIntervalVariable = "BM3D_TEST_HEARTBEAT_MS";

// More than any machine has cores; a larger value is a mistake, not a request
constexpr int kMaxThreads = 4096;

// A path from the command line, as UTF-8
std::filesystem::path pathFromUtf8(const std::string& utf8) {
    return std::filesystem::u8path(utf8);
}

// A whole number of the given decimal digits (at most 9) and nothing else; -1 when it is not
int parseNumber(const std::string& value) {
    if (value.empty() || value.size() > 9)
        return -1;
    int n = 0;
    for (const char c : value) {
        if (c < '0' || c > '9')
            return -1;
        n = n * 10 + (c - '0');
    }
    return n;
}

// A thread count: decimal digits only, from 1 to kMaxThreads; anything else is 0
int parseThreads(const std::string& value) {
    const int n = parseNumber(value);
    return n >= 1 && n <= kMaxThreads ? n : 0;
}

std::chrono::milliseconds heartbeatInterval() {
    const char* text = std::getenv(kHeartbeatIntervalVariable);
    const int ms = text ? parseNumber(text) : -1;
    return ms > 0 ? std::chrono::milliseconds(ms) : kHeartbeatInterval;
}

void printUsage(std::ostream& out) {
    out << "Usage: " << kProgram << " [--threads N] [--metadata <file.json>] <input> <output>\n"
        << "       " << kProgram << " --version\n"
        << "       " << kProgram << " --help\n"
        << "Builds a multiresolution model from an OBJ, PLY or STL mesh (and the textures it names),\n"
        << "corto-compressed. The output is a .bm3d file, format " << bm3d::formatVersion() << ".\n"
        << "  --threads N          keep at most N threads busy, from 1 to " << kMaxThreads
        << " (default: one per core)\n"
        << "  --metadata <file>    a JSON object whose members are stored in the metadata of the file\n"
        << "                       (not generator, buildOptions, source or a name that starts with bm3d)\n"
        << "Exit codes: 0 done, 1 the build failed, 2 the input is missing or invalid, 64 wrong command line.\n";
}

int usageError(const std::string& message) {
    std::cerr << kProgram << ": " << message << std::endl;
    printUsage(std::cerr);
    return kExitUsage;
}

// Prints "bm3d: building <input> (<n> s)" at once and then every interval until stopped.
// Every line is flushed: the reader is usually a pipe.
class Heartbeat {
public:
    Heartbeat(std::string input, std::chrono::milliseconds interval)
        : input_(std::move(input)), interval_(interval), thread_([this] { run(); }) {}

    ~Heartbeat() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }

    Heartbeat(const Heartbeat&) = delete;
    Heartbeat& operator=(const Heartbeat&) = delete;

private:
    void run() {
        const auto start = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        do {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
            std::cout << kProgram << ": building " << input_ << " (" << elapsed.count() << " s)" << std::endl;
        } while (!wake_.wait_for(lock, interval_, [this] { return stop_; }));
    }

    std::string input_;
    std::chrono::milliseconds interval_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::thread thread_;  // last: starts running when the members above are ready
};

// Reads the metadata file into `text`. The reason it cannot be, or is not a JSON object, or an
// empty string when it is one
std::string readMetadata(const std::string& file, std::string& text) {
    std::ifstream in(pathFromUtf8(file), std::ios::binary);
    if (!in)
        return "cannot read the metadata file " + file;
    text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return "cannot read the metadata file " + file;
    const std::string problem = bm3d::jsonObjectError(text);
    return problem.empty() ? problem : "the metadata file " + file + " is not a JSON object: " + problem;
}

// args are the command line arguments without the program name, as UTF-8
int run(const std::vector<std::string>& args) {
    if (args.size() == 1 && args[0] == "--version") {
        std::cout << kProgram << " " << bm3d::version() << " (format " << bm3d::formatVersion() << ")" << std::endl;
        return kExitOk;
    }

    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
        printUsage(std::cout);
        return kExitOk;
    }

    bm3d::BuildOptions options;
    std::string metadata;
    bool hasThreads = false, hasMetadata = false;
    std::vector<std::string> paths;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--threads" || arg == "--metadata") {
            const bool isThreads = arg == "--threads";
            if (isThreads ? hasThreads : hasMetadata)
                return usageError(arg + " is given twice");
            if (i + 1 >= args.size())
                return usageError(arg + " needs a value");
            const std::string& value = args[++i];
            if (isThreads) {
                const int threads = parseThreads(value);
                if (threads == 0)
                    return usageError("--threads takes a whole number from 1 to " + std::to_string(kMaxThreads) +
                                      ", not '" + value + "'");
                options.threads = threads;
                hasThreads = true;
            } else {
                metadata = value;
                hasMetadata = true;
            }
        } else if (arg == "--") {
            paths.insert(paths.end(), args.begin() + i + 1, args.end());
            break;
        } else if (arg.size() > 1 && arg[0] == '-' && arg != "-") {
            return usageError("unknown option " + arg);
        } else {
            paths.push_back(arg);
        }
    }

    if (paths.size() != 2)
        return usageError(paths.size() < 2 ? "an input and an output are needed" : "too many arguments");

    if (hasMetadata) {
        std::string problem = readMetadata(metadata, options.metadata);
        if (problem.empty()) {
            problem = bm3d::metadataError(options.metadata);
            if (!problem.empty())
                problem = "the metadata file " + metadata + ": " + problem;
        }
        if (!problem.empty())
            return usageError(problem);
    }

    const std::string& input = paths[0];
    const std::string& output = paths[1];

    // The warnings of the build go to the standard error stream, one line each
    options.logger = [](const std::string& message) { std::cerr << "Warning: " << message << std::endl; };

    try {
        Heartbeat heartbeat(input, heartbeatInterval());
        bm3d::build(pathFromUtf8(input), pathFromUtf8(output), options);
        return kExitOk;
    } catch (const bm3d::Error& error) {
        switch (error.kind()) {
            case bm3d::ErrorKind::InvalidInput:
                std::cerr << kProgram << ": invalid input: " << error.what() << std::endl;
                return kExitInvalidInput;
            case bm3d::ErrorKind::InvalidArgument:
                return usageError(error.what());
            case bm3d::ErrorKind::BuildFailed:
                break;
        }
        std::cerr << kProgram << ": could not build " << output << " from " << input << ": " << error.what()
                  << std::endl;
        return kExitBuildFailed;
    } catch (const std::exception& error) {
        std::cerr << kProgram << ": could not build " << output << " from " << input << ": " << error.what()
                  << std::endl;
        return kExitBuildFailed;
    }
}

#ifdef _WIN32
// A UTF-16 command line argument as UTF-8
std::string toUtf8(const wchar_t* wide) {
    const int length = static_cast<int>(std::wcslen(wide));
    if (length == 0)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, length, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, length, &out[0], size, nullptr, nullptr);
    return out;
}
#endif

}  // namespace

#ifdef _WIN32

// The arguments arrive as UTF-16 so that a path in any script survives
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(toUtf8(argv[i]));
    return run(args);
}

#else

int main(int argc, char* argv[]) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}

#endif
