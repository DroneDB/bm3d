// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

namespace bm3dtest {

// Sets an environment variable for its lifetime and puts the previous state back after it (the
// variable absent, or its old value). Processes started meanwhile inherit it.
class EnvVarGuard {
public:
    EnvVarGuard(std::string name, const std::string& value);
    ~EnvVarGuard();

    EnvVarGuard(const EnvVarGuard&) = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

// Makes `folder` (UTF-8) the system temporary folder for its lifetime: the variables the
// standard library reads for it (TMPDIR; TMP and TEMP on Windows) are set to it
class TemporaryFolderGuard {
public:
    explicit TemporaryFolderGuard(const std::string& folder);

private:
    EnvVarGuard tmpdir_;
    EnvVarGuard tmp_;
    EnvVarGuard temp_;
};

}  // namespace bm3dtest
