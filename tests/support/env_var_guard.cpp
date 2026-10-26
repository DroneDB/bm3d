// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "env_var_guard.h"

#include <cstdlib>
#include <utility>

namespace bm3dtest {

namespace {

// `value` null removes the variable
void setVariable(const std::string& name, const char* value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value != nullptr ? value : "");
#else
    if (value != nullptr)
        setenv(name.c_str(), value, 1);
    else
        unsetenv(name.c_str());
#endif
}

}  // namespace

std::optional<std::string> readEnvironmentVariable(const std::string& name) {
#ifdef _WIN32
    // getenv is flagged as unsafe by MSVC and there is no need for the thread safety it lacks
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name.c_str()) == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result;
    }
    return std::nullopt;
#else
    if (const char* value = std::getenv(name.c_str()))
        return std::string(value);
    return std::nullopt;
#endif
}

EnvVarGuard::EnvVarGuard(std::string name, const std::string& value)
    : name_(std::move(name)), previous_(readEnvironmentVariable(name_)) {
    setVariable(name_, value.c_str());
}

EnvVarGuard::~EnvVarGuard() {
    setVariable(name_, previous_ ? previous_->c_str() : nullptr);
}

TemporaryFolderGuard::TemporaryFolderGuard(const std::string& folder)
    : tmpdir_("TMPDIR", folder), tmp_("TMP", folder), temp_("TEMP", folder) {}

}  // namespace bm3dtest
