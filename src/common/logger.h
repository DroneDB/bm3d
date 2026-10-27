// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the warnings of a build go. The library writes nothing to the standard streams: the
// caller of the build gives a function (BuildOptions::logger) and the code that has something to
// say calls it.

#ifndef BM3D_LOGGER_H
#define BM3D_LOGGER_H

#include <functional>
#include <string>

namespace bm3d::detail {

//Called with the text of a warning (UTF-8, without a prefix and without a line end), from the
//thread that runs the build. An empty function drops the warnings.
using Logger = std::function<void(const std::string &message)>;

inline void warn(const Logger &logger, const std::string &message) {
	if(logger)
		logger(message);
}

} //namespace

#endif // BM3D_LOGGER_H
