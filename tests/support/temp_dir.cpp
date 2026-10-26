// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temp_dir.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <random>
#include <stdexcept>
#include <system_error>

#include <gtest/gtest.h>

#ifdef _WIN32
#include <process.h>
#define BM3D_TEST_GETPID _getpid
#else
#include <unistd.h>
#define BM3D_TEST_GETPID getpid
#endif

namespace bm3dtest {

namespace {

// The name of the running test, fit for a file name (a parameterized test has a '/')
std::string currentTestName() {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    if (info == nullptr)
        return "test";
    std::string name = std::string(info->test_suite_name()) + "." + info->name();
    std::replace_if(
        name.begin(), name.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '.'; }, '_');
    return name.substr(0, 80);
}

}  // namespace

TempDir::TempDir() {
    static std::atomic<unsigned> counter{0};
    std::random_device random;
    const fs::path root = fs::temp_directory_path();
    // Not found by chance in the folder of another process: the process id, a counter and a
    // random number, so that tests run in parallel by ctest never share a folder
    for (int attempt = 0; attempt < 100; ++attempt) {
        const fs::path candidate = root / ("bm3d-test-" + currentTestName() + "-" +
                                           std::to_string(BM3D_TEST_GETPID()) + "-" + std::to_string(counter++) + "-" +
                                           std::to_string(random()));
        std::error_code ec;
        if (fs::create_directory(candidate, ec)) {
            path_ = candidate;
            return;
        }
    }
    throw std::runtime_error("cannot create a temporary folder in " + pathToUtf8(root));
}

TempDir::~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
}

fs::path TempDir::file(const std::string& relative) const {
    return path_ / pathFromUtf8(relative);
}

fs::path TempDir::makeDir(const std::string& relative) const {
    const fs::path dir = file(relative);
    fs::create_directories(dir);
    return dir;
}

std::vector<std::string> TempDir::list(const std::string& relative) const {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(relative.empty() ? path_ : file(relative)))
        names.push_back(pathToUtf8(entry.path().filename()));
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace bm3dtest
