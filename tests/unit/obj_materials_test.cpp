// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The textures the materials of an OBJ name: a texture is kept once for each image that cuts it out
// (map_d), so that a texture used by one material with a cutout and by another without is two.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "io/mesh/objloader.h"
#include "model_files.h"
#include "pictures.h"
#include "temp_dir.h"

namespace {

using namespace bm3dtest;

TEST(ObjMaterials, aTextureIsKeptOnceForEachAlphaItIsCutOutWith) {
    TempDir dir;
    writePng(dir.file("texture.png"), gradientRaster(4));
    writePng(dir.file("mask.png"), gradientRaster(1));
    writeTextFile(dir.file("m.mtl"), "newmtl cut\nmap_Kd texture.png\nmap_d texture.png\n"
                                     "newmtl plain\nmap_Kd texture.png\n"
                                     "newmtl again\nmap_Kd texture.png\nmap_d texture.png\n"
                                     "newmtl masked\nmap_Kd texture.png\nmap_d mask.png\n");
    writeTextFile(dir.file("m.obj"), "mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl cut\nf 1 2 3\n");
    ObjLoader loader(pathToUtf8(dir.file("m.obj")), "");
    const std::vector<LoadTexture>& textures = loader.textureFilenames();
    ASSERT_EQ(textures.size(), 3u);
    EXPECT_EQ(textures[0].filename, pathToUtf8(dir.file("texture.png")));
    EXPECT_EQ(textures[0].alphaFilename, textures[0].filename);
    EXPECT_EQ(textures[1].filename, textures[0].filename);
    EXPECT_FALSE(textures[1].isCutout());
    EXPECT_EQ(textures[2].alphaFilename, pathToUtf8(dir.file("mask.png")));
}

// A map_d without a map_Kd has no texture to cut out: the material is drawn in its colour
TEST(ObjMaterials, anAlphaWithoutATextureIsNoTexture) {
    TempDir dir;
    writePng(dir.file("mask.png"), gradientRaster(1));
    writeTextFile(dir.file("m.mtl"), "newmtl colour\nKd 1 0 0\nmap_d mask.png\n");
    writeTextFile(dir.file("m.obj"), "mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl colour\nf 1 2 3\n");
    ObjLoader loader(pathToUtf8(dir.file("m.obj")), "");
    EXPECT_TRUE(loader.textureFilenames().empty());
    EXPECT_FALSE(loader.hasTextures());
    EXPECT_TRUE(loader.hasColors());
}

}  // namespace
