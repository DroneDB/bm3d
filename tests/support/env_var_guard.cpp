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

EnvVarGuard::EnvVarGuard(std::string name, const std::string& value) : name_(std::move(name)) {
#ifdef _WIN32
    // getenv is flagged as unsafe by MSVC and there is no need for the thread safety it lacks
    char* old = nullptr;
    size_t size = 0;
    if (_dupenv_s(&old, &size, name_.c_str()) == 0 && old != nullptr) {
        previous_ = std::string(old);
        std::free(old);
    }
#else
    if (const char* old = std::getenv(name_.c_str()))
        previous_ = std::string(old);
#endif
    setVariable(name_, value.c_str());
}

EnvVarGuard::~EnvVarGuard() {
    setVariable(name_, previous_ ? previous_->c_str() : nullptr);
}

TemporaryFolderGuard::TemporaryFolderGuard(const std::string& folder)
    : tmpdir_("TMPDIR", folder), tmp_("TMP", folder), temp_("TEMP", folder) {}

}  // namespace bm3dtest
