// bm3d: builds a multiresolution 3D model from a mesh.
// Copyright (C) Digipa. Licensed under the GNU General Public License, version 3 (see LICENSE).
// It builds on Nexus (GPL-2.0-or-later) and VCGLib (GPL-3.0). It derives from nxs.cpp of libnexus
// (DroneDB; written by Piero Toffanin, Luca Di Leo and HeDo), which wrapped the Nexus builder in
// a C interface and is licensed under version 3 of the GPL only, as this file is.
// SPDX-License-Identifier: GPL-3.0-only

#include "bm3d/bm3d.h"

#include <locale.h>

#include <string>
#include <vector>

#include "bm3d/version.h"
#include "error_mapping.h"
#include "../build/build_pipeline.h"
#include "../common/error.h"
#include "../common/pathutil.h"
#include "../format/bm3d_format.h"
#include "../format/metadata.h"

namespace bm3d {

namespace {

static_assert(BM3D_FORMAT_VERSION == detail::kFormatVersion, "the format the library writes is the one src/format implements");

// More than any machine has cores; a larger value is a mistake, not a request
constexpr int kMaxThreads = 4096;

// RAII: set LC_NUMERIC to "C" during this scope and restore previous value.
class NumericLocaleGuard {
public:
    NumericLocaleGuard() {
        const char* current = std::setlocale(LC_NUMERIC, nullptr);
        if (current) {
            oldLocale_ = current;
        }
        std::setlocale(LC_NUMERIC, "C");
    }

    ~NumericLocaleGuard() {
        if (!oldLocale_.empty()) {
            std::setlocale(LC_NUMERIC, oldLocale_.c_str());
        }
    }

    NumericLocaleGuard(const NumericLocaleGuard&) = delete;
    NumericLocaleGuard& operator=(const NumericLocaleGuard&) = delete;

private:
    std::string oldLocale_;
};

// The members to store, from the text of BuildOptions::metadata. Returns what is wrong with it:
// not a JSON object, a key that is the program's or repeated, or so large that the block cannot
// fit in a file whatever the input is (the smallest block the program writes, with these members)
std::string parseMetadata(const std::string& json, std::vector<JsonMember>& extras) {
    if (json.empty())
        return {};
    const std::string problem = detail::parseExtraMetadata(json, extras);
    if (!problem.empty())
        return problem;
    return detail::BuildPipeline(detail::BuildSettings(), detail::Logger()).metadataProblem(extras);
}

// The input must name an existing file. Turning a path that is not valid UTF-8 into a file
// system path throws on Windows, hence the try.
void requireInput(const std::string& input) {
    try {
        if (!detail::pathExists(input))
            throw Error(ErrorKind::InvalidInput, "Input file does not exist: " + input);
    } catch (const Error&) {
        throw;
    } catch (...) {
        throw Error(ErrorKind::InvalidInput, "Input path is not valid: " + detail::describeCurrentException());
    }
}

}  // namespace

void build(const std::filesystem::path& input, const std::filesystem::path& output, const BuildOptions& options) {
    if (options.threads < 0 || options.threads > kMaxThreads)
        throw Error(ErrorKind::InvalidArgument,
                    "threads must be from 1 to " + std::to_string(kMaxThreads) + ", or 0 for one per core");
    if (input.empty())
        throw Error(ErrorKind::InvalidInput, "Input path is empty");
    if (output.empty())
        throw Error(ErrorKind::InvalidArgument, "Output path is empty");

    // The metadata to store is checked before anything is built
    std::vector<JsonMember> extraMetadata;
    const std::string metadataProblem = parseMetadata(options.metadata, extraMetadata);
    if (!metadataProblem.empty())
        throw Error(ErrorKind::InvalidArgument, metadataProblem);

    std::string inputPath, outputPath;
    try {
        inputPath = detail::pathToUtf8(input);
        outputPath = detail::pathToUtf8(output);
    } catch (...) {
        throw Error(ErrorKind::InvalidArgument, "Path is not valid: " + detail::describeCurrentException());
    }

    requireInput(inputPath);

    try {
        // Use "C" numeric locale for this operation only.
        NumericLocaleGuard numericLocaleGuard;

        detail::BuildPipeline(detail::BuildSettings(), options.logger).build(inputPath, outputPath, options.threads, extraMetadata);
    } catch (...) {
        detail::rethrowAsApiError();
    }
}

std::string metadataError(const std::string& json) {
    std::vector<JsonMember> extras;
    return parseMetadata(json, extras);
}

const char* version() noexcept {
    return BM3D_VERSION;
}

int formatVersion() noexcept {
    return BM3D_FORMAT_VERSION;
}

}  // namespace bm3d
