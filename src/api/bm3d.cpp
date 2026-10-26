// bm3d: builds a multiresolution 3D model from a mesh.
// Copyright (C) Digipa. Licensed under the GNU General Public License, version 3 (see LICENSE).
// It builds on Nexus (GPL-2.0-or-later) and VCGLib (GPL-3.0). It derives from nxs.cpp of libnexus
// (DroneDB; written by Piero Toffanin, Luca Di Leo and HeDo), which wrapped the Nexus builder in
// a C interface and is licensed under version 3 of the GPL only, as this file is.
// SPDX-License-Identifier: GPL-3.0-only

#include "bm3d/bm3d.h"

#include <locale.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "bm3d/version.h"
#include "../common/error.h"
#include "../common/nexusdata.h"
#include "../common/pathutil.h"
#include "../common/traversal.h"
#include "../nxsbuild/nexusbuilder.h"
#include "../nxsedit/extractor.h"
#include "kdtree.h"
#include "meshstream.h"

namespace bm3d {

namespace {

using nx::NexusData;
using nx::Signature;

// More than any machine has cores; a larger value is a mistake, not a request
constexpr int kMaxThreads = 4096;

// The build settings. A model is built the same way whoever asks; only the threads vary.
constexpr int kNodeFaces = 1 << 14;        // triangles per node
constexpr int kTopNodeFaces = 4096;        // triangles of the root node
constexpr int kTextureQuality = 90;        // WebP quality of the node textures
constexpr int kMaxNodeTextureSide = 1024;  // longest side of a node texture
constexpr int kRamBufferMb = 16000;        // approximate RAM budget
constexpr float kScaling = 0.5f;           // simplification ratio per level
constexpr int kSkipLevels = -1;            // levels that keep the geometry and halve the texture (-1: as the texture needs)
constexpr float kAdaptive = 0.333f;        // kd-tree adaptive factor

// corto quantization: the coordinate step comes from the error of the nodes
constexpr float kErrorQ = 0.1f;
constexpr int kLumaBits = 6;
constexpr int kChromaBits = 6;
constexpr int kAlphaBits = 5;
constexpr int kNormalBits = 10;
constexpr float kTexStep = 0.25f;  // in pixels

// The text of the exception being handled; call it from a catch block. Besides the standard
// exceptions there is corto, the mesh codec, which throws its errors as C strings.
std::string describeCurrentException() {
    try {
        throw;
    } catch (const nx::Error& e) {
        return e.what();
    } catch (const std::exception& e) {
        return std::string("Exception: ") + e.what();
    } catch (const char* message) {
        return message;
    } catch (...) {
        return "Unknown exception";
    }
}

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

// RAII: remove a file on destruction if path is not empty.
class TempFileGuard {
public:
    TempFileGuard() = default;

    void setPath(const std::string& path) { path_ = path; }
    // The file is complete: leave it in place
    void keep() { path_.clear(); }

    ~TempFileGuard() {
        if (path_.empty())
            return;
        // A destructor must not throw, and the path conversion can (see build). A folder
        // is never what the guard is for: it is the path of an output that could not be opened.
        try {
            const auto path = nx::nativePath(path_);
            std::error_code ec;
            if (!std::filesystem::is_directory(path, ec))
                std::filesystem::remove(path, ec);
        } catch (...) {
        }
    }

    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;

private:
    std::string path_;
};

// The share of a node texture that the texture charts of the node's triangles fill once packed.
// On a photogrammetry model (224k triangles, 1.6 gigapixels of texture) nodes cut at this share
// of the largest node texture left 4% of the texels to the downscaling that makes them fit.
constexpr double NODE_TEXTURE_FILL = 0.35;

// The weight of a texel in the kd-tree, which splits a node when the weight of its texels passes
// the triangles of a node: a node holds the texels that fill its texture without downscaling, at
// most kMaxNodeTextureSide on a side.
float texelWeight(int nodeFaces) {
    const double side = double(kMaxNodeTextureSide);
    return float(double(nodeFaces) / (side * side * NODE_TEXTURE_FILL));
}

// What one attempt at building a model differs in from the next (see buildWithFallbacks)
struct Attempt {
    int threads = 1;
    int nodeFaces = kNodeFaces;
    bool withTextures = true;
};

// The name the OBJ loader gives its vertex cache. It has no folder, so VirtualMemory puts the
// file in the system temporary folder, not next to the output like the other scratch files.
constexpr const char* kObjVertexCacheName = "cache_plyvertex";

// The scratch files that reading the input writes, and how to tell their failures from those of
// the input. The Nexus code reports a scratch file it cannot create, grow or map with the same
// nx::Error as a mesh it cannot parse, and the message names the file (see MappedFile). The
// scratch files are named after the prefixes added here (one unique to the build, the fixed
// names of the OBJ loader and of the texture atlas), which no file of the input carries, so a
// message that names one is a failure of the machine: a disk or a temporary folder that cannot
// take the file, an address space that cannot map it.
class ScratchFiles {
public:
    ScratchFiles() {
        // A temporary folder that cannot be found fails the OBJ loader with the very error that
        // nx::temporaryDirectory gives here, which is kept to be recognised
        try {
            prefixes_.push_back(nx::joinPath(nx::temporaryDirectory(), kObjVertexCacheName));
        } catch (const nx::Error& e) {
            noTemporaryFolder_ = e.what();
        }
    }

    // A file named `prefix` followed by anything; an empty prefix names no file
    void add(const std::string& prefix) {
        if (!prefix.empty())
            prefixes_.push_back(prefix);
    }

    // Whether `message`, the text of an nx::Error, reports a failure of one of these files
    bool reportedIn(const std::string& message) const {
        if (!noTemporaryFolder_.empty() && message == noTemporaryFolder_)
            return true;
        for (const std::string& prefix : prefixes_)
            if (message.find(prefix) != std::string::npos)
                return true;
        return false;
    }

private:
    std::vector<std::string> prefixes_;
    std::string noTemporaryFolder_;
};

// Runs one step of reading the input (the mesh, its textures) and says whose fault a failure is.
// The input's as a rule: a file that cannot be opened, parsed or decoded. The machine's when
// memory runs out (std::bad_alloc) or a scratch file in `scratch` cannot be written: the input
// may well be fine, so that is a failed build. The standard library throws nothing else here
// that is not about the input: the file system calls of the reading code take an error_code,
// and what does throw (std::system_error) is the conversion of a name the input holds, a texture
// name that is not UTF-8, into a path.
template <typename Read>
void readInput(const ScratchFiles& scratch, Read&& read) {
    try {
        read();
    } catch (const Error&) {
        throw;  // already classified
    } catch (const std::bad_alloc&) {
        throw Error(ErrorKind::BuildFailed, describeCurrentException());
    } catch (const nx::Error& e) {
        throw Error(scratch.reportedIn(e.what()) ? ErrorKind::BuildFailed : ErrorKind::InvalidInput, e.what());
    } catch (...) {
        throw Error(ErrorKind::InvalidInput, describeCurrentException());
    }
}

// The builder computes in floats the squares of the lengths in a model and the products of its
// edges (the bounding sphere, the cone of the normals, the quadrics of the simplification), and
// those overflow when the model is some billions of units across (a city is 1e7 millimetres). The
// bounding sphere of a model beyond that is not a number and the search for it never ended: that
// is what a file of random bytes read as floats did.
constexpr double kMaxModelSize = 1e9;

// Reads the input into the out-of-core stream. A file the loaders accept without finding a
// triangle in it (a text file with an .obj name, say), or whose triangles span more than the
// builder can measure, is a problem of the input as well.
void loadInput(StreamSoup& stream, const std::string& input, const ScratchFiles& scratch) {
    readInput(scratch, [&] {
        std::string mtl;  // not exposed yet
        stream.load(std::vector<std::string>{input}, mtl);
    });
    if (stream.size() == 0)
        throw Error(ErrorKind::InvalidInput, "The input holds no triangles: " + input);

    // in double, where the square of the largest float difference still fits
    double squared = 0;
    for (int k = 0; k < 3; k++) {
        const double extent = double(stream.box.max[k]) - double(stream.box.min[k]);
        squared += extent * extent;
    }
    if (!(std::sqrt(squared) <= kMaxModelSize)) {
        std::ostringstream message;
        message << "The model is " << std::setprecision(3) << std::sqrt(squared) << " units across (the diagonal of its "
                << "bounding box); the largest that can be built is " << kMaxModelSize << ": " << input;
        throw Error(ErrorKind::InvalidInput, message.str());
    }
}

// Reads the textures the materials of the input name, which are part of the input: one that is
// missing or cannot be decoded is invalid input, as an unreadable mesh is
void loadTextures(NexusBuilder& builder, std::vector<LoadTexture>& textures, const ScratchFiles& scratch) {
    readInput(scratch, [&] {
        if (!builder.initAtlas(textures))
            throw nx::Error("Failed to initialize texture atlas");
    });
}

// The coordinate step of the compression: a fraction of the smallest error among the nodes that
// the sink follows.
float coordinateStep(NexusData& nexus) {
    const uint32_t sink = nexus.header.n_nodes - 1;
    float step = kErrorQ * nexus.nodes[0].error / 2.0f;
    for (uint32_t i = 0; i < sink; ++i) {
        nx::Node& n = nexus.nodes[i];
        nx::Patch& patch = nexus.patches[n.first_patch];
        if (patch.node != sink)
            continue;
        const double e = kErrorQ * n.error / 2.0;
        if (e < step && e > 0.0)
            step = static_cast<float>(e);
    }

    // Fallback to avoid log2(0) if the step is still <= 0
    if (step <= 0.0f) {
        step = 1.0f;
    }
    return step;
}

// Compresses the uncompressed container `source` with corto into `target`
void compress(const std::string& source, const std::string& target) {
    NexusData nexus;  // its constructor creates and owns the file reader

    if (!nexus.open(source.c_str())) {
        throw nx::Error("Could not open file: " + source);
    }

    Signature signature = nexus.header.signature;
    signature.flags |= Signature::CORTO;

    Extractor extractor(&nexus);
    extractor.coord_q = static_cast<int>(std::log2(coordinateStep(nexus)));
    extractor.norm_bits = kNormalBits;

    extractor.color_bits[0] = kLumaBits;
    extractor.color_bits[1] = kChromaBits;
    extractor.color_bits[2] = kChromaBits;
    extractor.color_bits[3] = kAlphaBits;

    extractor.tex_step = kTexStep;

    extractor.save(target, signature);
}

// Builds the model into the Nexus container with corto-compressed nodes; throws on failure.
void buildOnce(const std::string& input, const std::string& output, const Attempt& attempt) {
    const uint64_t maxMemory = (uint64_t(1) << 20) * static_cast<uint64_t>(kRamBufferMb) / 4;

    // Build a somewhat unique base name for cache files.
    // Place the out-of-core cache (stream + kd-tree) in the same directory as
    // the output, which under the DroneDB build pipeline is the per-build temp
    // folder. This keeps the (potentially multi-GB) scratch on the same large
    // volume as the result and lets it be reclaimed together with that folder,
    // instead of defaulting to the system temp dir. VirtualMemory uses an
    // absolute template prefix as-is.
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now().time_since_epoch().count();
    const std::string cacheDir = nx::absoluteParentPath(output);
    const std::string cacheBase = cacheDir + "/nexus_cache_" + std::to_string(static_cast<unsigned long long>(now));

    // Declared before the builder, so that it outlives it as it always did
    std::unique_ptr<KDTreeSoup> tree;

    // Every scratch file of the stream and of the PLY loader is named after cacheBase
    ScratchFiles scratch;
    scratch.add(cacheBase);

    StreamSoup stream(cacheBase + "_stream");
    stream.setMaxMemory(maxMemory);
    loadInput(stream, input, scratch);

    uint32_t components = NexusBuilder::FACES | NexusBuilder::NORMALS;
    if (stream.hasColors())
        components |= NexusBuilder::COLORS;
    if (stream.hasTextures() && attempt.withTextures)
        components |= NexusBuilder::TEXTURES;

    // Do not keep textures if they are not going to be used
    if (!(components & NexusBuilder::TEXTURES)) {
        stream.textures.clear();
    }

    // The output is written under another name in its folder and given its name only once it
    // is complete: a save that fails (or a process killed while saving) leaves no half file
    // under the output name, and does not remove one an earlier build left there. The builder
    // writes the uncompressed container first and the compression reads it back.
    const std::string writtenOutput = output + ".part";
    const std::string builderOutput = output + ".tmp.nxs";

    // Remove the temporary files the saves below write, whatever happens. They are declared
    // before the builder, which keeps its file open until it is destroyed when its save throws:
    // on Windows an open file cannot be removed.
    TempFileGuard writtenOutputGuard;
    TempFileGuard temporaryNxsGuard;
    writtenOutputGuard.setPath(writtenOutput);
    temporaryNxsGuard.setPath(builderOutput);

    // Place the builder's out-of-core caches (chunks + per-node texture file)
    // in the same directory as the output (the per-build temp folder), so they
    // do not leak into the process working directory and are reclaimed with it.
    NexusBuilder builder(components, cacheDir);
    builder.skipSimplifyLevels = kSkipLevels;
    builder.setMaxMemory(maxMemory);

    builder.setScaling(kScaling);

    builder.tex_quality = kTextureQuality;
    builder.textureThreads = attempt.threads;
    builder.maxNodeTexSide = kMaxNodeTextureSide;

    // The atlas keeps the texture tiles that do not fit in memory in a scratch file of its own
    scratch.add(builder.atlas.storage_prefix);
    loadTextures(builder, stream.textures, scratch);

    tree.reset(new KDTreeSoup(cacheBase + "_tree", kAdaptive));
    tree->setMaxMemory((uint64_t(1) << 20) * static_cast<uint64_t>(kRamBufferMb) / 2);
    tree->setMaxWeight(attempt.nodeFaces);
    tree->texelWeight = texelWeight(attempt.nodeFaces);
    tree->setTrianglesPerBlock(attempt.nodeFaces);

    // Build the hierarchy
    builder.create(tree.get(), &stream, kTopNodeFaces);

    builder.save(builderOutput);
    compress(builderOutput, writtenOutput);

    std::error_code ec;
    std::filesystem::rename(nx::nativePath(writtenOutput), nx::nativePath(output), ec);
    if (ec)
        throw nx::Error("could not rename " + writtenOutput + " to " + output + ": " + ec.message());
    writtenOutputGuard.keep();
}

// How many times a build whose nodes turn out to have too many vertices is tried again, each
// time with half as many triangles per node as the time before
constexpr int kSmallerNodeAttempts = 3;

// Builds the model, falling back to what the input allows; throws on failure. Each attempt
// that fails leaves nothing behind: it fails while building the hierarchy, and its scratch files
// and partial outputs go with it (see buildOnce), so the next one starts from scratch.
//  - A node needs a texture too large to pack: the model is built without textures. Models of
//    this kind always built that way.
//  - A node has more vertices than a node holds (a fragmented texture parametrization): the
//    model is built with smaller nodes. Leaving the node out, which Nexus did, leaves a hole in
//    the model.
void buildWithFallbacks(const std::string& input, const std::string& output, int threads) {
    Attempt attempt;
    attempt.threads = threads > 0 ? threads : int(std::max(1u, std::thread::hardware_concurrency()));
    int smallerNodeAttempts = 0;
    for (;;) {
        try {
            buildOnce(input, output, attempt);
            break;
        } catch (const nx::TexturePackingError& error) {
            if (!attempt.withTextures)
                throw;
            std::cerr << "Warning: " << error.what() << std::endl;
            attempt.withTextures = false;
        } catch (const nx::NodeTooLargeError& error) {
            if (smallerNodeAttempts == kSmallerNodeAttempts)
                throw nx::Error(std::string(error.what()) + "; nodes of at most " +
                                std::to_string(attempt.nodeFaces) +
                                " triangles, the smallest tried (the build started with " +
                                std::to_string(kNodeFaces) + "), are still too large for this model");
            std::cerr << "Warning: " << error.what() << std::endl;
            attempt.nodeFaces /= 2;
            ++smallerNodeAttempts;
        }
    }

    if (attempt.nodeFaces != kNodeFaces)
        std::cerr << "Warning: built with nodes of " << attempt.nodeFaces << " triangles instead of " << kNodeFaces
                  << ": the texture parametrization is too fragmented for larger nodes" << std::endl;
    if (!attempt.withTextures)
        std::cerr << "Warning: textures could not be packed; built without textures" << std::endl;
}

// The input must name an existing file. Turning a path that is not valid UTF-8 into a file
// system path throws on Windows, hence the try.
void requireInput(const std::string& input) {
    try {
        if (!nx::pathExists(input))
            throw Error(ErrorKind::InvalidInput, "Input file does not exist: " + input);
    } catch (const Error&) {
        throw;
    } catch (...) {
        throw Error(ErrorKind::InvalidInput, "Input path is not valid: " + describeCurrentException());
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

    std::string inputPath, outputPath;
    try {
        inputPath = nx::pathToUtf8(input);
        outputPath = nx::pathToUtf8(output);
    } catch (...) {
        throw Error(ErrorKind::InvalidArgument, "Path is not valid: " + describeCurrentException());
    }

    requireInput(inputPath);

    try {
        // Use "C" numeric locale for this operation only.
        NumericLocaleGuard numericLocaleGuard;

        buildWithFallbacks(inputPath, outputPath, options.threads);
    } catch (const Error&) {
        throw;  // already classified
    } catch (...) {
        throw Error(ErrorKind::BuildFailed, describeCurrentException());
    }
}

const char* version() noexcept {
    return BM3D_VERSION;
}

int formatVersion() noexcept {
    return BM3D_FORMAT_VERSION;
}

}  // namespace bm3d
