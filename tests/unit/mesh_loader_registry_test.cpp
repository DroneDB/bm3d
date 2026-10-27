// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The registry of mesh loaders: the format of a file is told by the ending of its name, in any
// case, and a format is added without the code that reads files knowing it.

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "common/error.h"
#include "io/mesh/mesh_loader_registry.h"

namespace {

using namespace bm3d::detail;

// A loader that reads nothing and says which factory made it
class FakeLoader : public MeshLoader {
public:
    explicit FakeLoader(std::string made) : made(std::move(made)) {}
    void setMaxMemory(uint64_t) override {}
    uint32_t getTriangles(uint32_t, Triangle*) override { return 0; }
    uint64_t vertexCount() const override { return 0; }
    std::string made;
};

MeshLoaderRegistry::Factory fake(const std::string& name) {
    return [name](const std::string&, const MeshLoaderContext&) { return std::unique_ptr<MeshLoader>(new FakeLoader(name)); };
}

std::string madeBy(const MeshLoaderRegistry& registry, const std::string& file) {
    const std::unique_ptr<MeshLoader> loader = registry.create(file, MeshLoaderContext());
    return static_cast<FakeLoader&>(*loader).made;
}

TEST(MeshLoaderRegistry, aFileGoesToTheLoaderOfItsExtension) {
    MeshLoaderRegistry registry;
    registry.add(".aaa", fake("a"));
    registry.add(".bbb", fake("b"));
    EXPECT_EQ(madeBy(registry, "model.aaa"), "a");
    EXPECT_EQ(madeBy(registry, "/some/folder/model.bbb"), "b");
}

TEST(MeshLoaderRegistry, theCaseOfTheExtensionDoesNotMatter) {
    MeshLoaderRegistry registry;
    registry.add(".aaa", fake("a"));
    EXPECT_EQ(madeBy(registry, "MODEL.AaA"), "a");
}

TEST(MeshLoaderRegistry, whenTwoEndingsOverlapTheFirstAddedIsUsed) {
    MeshLoaderRegistry registry;
    registry.add(".aaa", fake("first"));
    registry.add(".tar.aaa", fake("second"));
    registry.add(".aaa", fake("third"));
    EXPECT_EQ(madeBy(registry, "model.tar.aaa"), "first");
}

TEST(MeshLoaderRegistry, theFactoryGetsTheFileAndTheContext) {
    MeshLoaderRegistry registry;
    std::string seenFile, seenMtl, seenScratch;
    registry.add(".aaa", [&](const std::string& file, const MeshLoaderContext& context) {
        seenFile = file;
        seenMtl = context.materialLibrary;
        seenScratch = context.scratchPrefix;
        return std::unique_ptr<MeshLoader>(new FakeLoader("a"));
    });
    MeshLoaderContext context;
    context.materialLibrary = "lib.mtl";
    context.scratchPrefix = "/scratch/prefix";
    registry.create("dir/model.aaa", context);
    EXPECT_EQ(seenFile, "dir/model.aaa");
    EXPECT_EQ(seenMtl, "lib.mtl");
    EXPECT_EQ(seenScratch, "/scratch/prefix");
}

TEST(MeshLoaderRegistry, anUnknownExtensionIsRefusedNamingTheFormatsRead) {
    MeshLoaderRegistry registry;
    registry.add(".aaa", fake("a"));
    registry.add(".bbb", fake("b"));
    registry.add(".ccc", fake("c"));
    try {
        registry.create("model.xyz", MeshLoaderContext());
        FAIL() << "a file of an unknown type was accepted";
    } catch (const Error& e) {
        EXPECT_STREQ(e.what(), "unsupported input file model.xyz: the formats read are aaa, bbb and ccc, told by the "
                               "extension of the name");
    }
}

TEST(MeshLoaderRegistry, aRegistryWithNoLoaderRefusesEveryFile) {
    EXPECT_THROW(MeshLoaderRegistry().create("model.obj", MeshLoaderContext()), Error);
}

TEST(MeshLoaderRegistry, theStandardRegistryReadsObjPlyAndStl) {
    const MeshLoaderRegistry& registry = MeshLoaderRegistry::standard();
    EXPECT_EQ(registry.extensions(), (std::vector<std::string>{".obj", ".ply", ".stl"}));
    try {
        registry.create("model.fbx", MeshLoaderContext());
        FAIL() << "a file of an unknown type was accepted";
    } catch (const Error& e) {
        EXPECT_NE(std::string(e.what()).find("the formats read are obj, ply and stl, told by the extension of the name"),
                  std::string::npos);
    }
}

}  // namespace
