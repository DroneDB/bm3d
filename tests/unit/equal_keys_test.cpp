// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The orders of the builder and of the encoder that see equal keys: each gives them an order of the
// input, where std::sort and the heap functions would give the order of the standard library, which
// is not the same with libstdc++ and with MSVC, and the same model would give two files. Each test
// gives many equal keys in a scrambled order, more than the 16 or 32 elements a library sorts by
// insertion (which happens to keep their order), and checks the order the input fixes.

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include <corto/tunstall.h>
#include <vcg/space/rect_packer.h>

#include "build/builder.h"
#include "build/mesh.h"
#include "build/nodepatches.h"

namespace {

using namespace bm3d::detail;

std::vector<int> scrambled(int n) {
    std::vector<int> order(n);
    for (int i = 0; i < n; i++)
        order[i] = i;
    std::mt19937 random(7);
    std::shuffle(order.begin(), order.end(), random);
    return order;
}

// --- the triangles of a node by the node of their patch ------------------------------------------

struct Face {
    uint32_t node;
    int id;
};

TEST(EqualKeys, theTrianglesOfAPatchKeepTheirOrder) {
    std::vector<Face> faces;
    const std::vector<int> ids = scrambled(300);
    for (int id : ids)
        faces.push_back({uint32_t(id % 3), id});
    std::vector<PatchRecord> patches;
    sortFacesIntoPatches(faces, patches);

    ASSERT_EQ(patches.size(), 3u);
    for (uint32_t p = 0; p < 3; p++) {
        EXPECT_EQ(patches[p].node, p);
        EXPECT_EQ(patches[p].triangleOffset, 100u * (p + 1));
        EXPECT_EQ(patches[p].texture, kNoTexture);
    }
    // within each patch, the order of the input
    std::vector<int> expected;
    for (uint32_t node = 0; node < 3; node++)
        for (int id : ids)
            if (uint32_t(id % 3) == node)
                expected.push_back(id);
    for (size_t i = 0; i < faces.size(); i++)
        ASSERT_EQ(faces[i].id, expected[i]) << "at " << i;
}

TEST(EqualKeys, aNodeOfNoTriangleHasNoPatch) {
    std::vector<Face> faces;
    std::vector<PatchRecord> patches;
    sortFacesIntoPatches(faces, patches);
    EXPECT_TRUE(patches.empty());
}

// --- the border vertices whose normals are made the same -----------------------------------------

TEST(EqualKeys, theVerticesOfOnePointAreOrderedByNodeThenIndex) {
    std::vector<NVertex> vertices;
    vcg::Point3s normal;
    const std::vector<int> order = scrambled(100);
    for (int i : order)
        vertices.emplace_back(uint32_t(i % 2), uint32_t(i), vcg::Point3f(1, 2, 3), &normal);
    vertices.emplace_back(0u, 500u, vcg::Point3f(0, 0, 0), &normal);  // a lower point comes first
    std::sort(vertices.begin(), vertices.end());

    EXPECT_EQ(vertices[0].index, 500u);
    // the highest node first, and the indices of one node in increasing order
    for (size_t k = 1; k < vertices.size(); k++) {
        const uint32_t expectedNode = k <= 50 ? 1 : 0;
        const uint32_t expectedIndex = uint32_t(2 * ((k - 1) % 50) + expectedNode);
        ASSERT_EQ(vertices[k].node, expectedNode) << "at " << k;
        ASSERT_EQ(vertices[k].index, expectedIndex) << "at " << k;
    }
}

// --- the edge collapses of the simplification ----------------------------------------------------

// A collapse that does nothing but have a priority
class Collapse : public vcg::LocalModification<Mesh> {
public:
    Collapse(float priority, int id) : priority_(priority), id_(id) {}
    vcg::ModifierType IsOfType() override { return vcg::TriEdgeCollapseOp; }
    bool IsUpToDate() const override { return true; }
    bool IsFeasible(vcg::BaseParameterClass*) override { return true; }
    ScalarType ComputePriority(vcg::BaseParameterClass*) override { return priority_; }
    ScalarType Priority() const override { return priority_; }
    void Execute(Mesh&, vcg::BaseParameterClass*) override {}
    void UpdateHeap(HeapType&, vcg::BaseParameterClass*) override {}
    int id() const { return id_; }

private:
    float priority_;
    int id_;
};

TEST(EqualKeys, collapsesOfTheSameCostAreDoneInTheOrderTheyWereMade) {
    using HeapElem = vcg::LocalOptimization<Mesh>::HeapElem;
    std::vector<HeapElem> heap;
    for (int id = 0; id < 100; id++)
        heap.push_back(HeapElem(new Collapse(id == 60 ? -1.0f : 0.0f, id)));
    // the heap is built and filled the ways the optimization does: all at once, then one at a time
    std::make_heap(heap.begin(), heap.end());
    for (int id = 100; id < 140; id++) {
        heap.push_back(HeapElem(new Collapse(0.0f, id)));
        std::push_heap(heap.begin(), heap.end());
    }

    std::vector<int> popped;
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end());
        popped.push_back(static_cast<Collapse*>(heap.back().locModPtr)->id());
        delete heap.back().locModPtr;
        heap.pop_back();
    }
    // the lowest cost first, then the rest in the order they were made
    ASSERT_EQ(popped.size(), 140u);
    EXPECT_EQ(popped[0], 60);
    std::vector<int> expected;
    for (int id = 0; id < 140; id++)
        if (id != 60)
            expected.push_back(id);
    EXPECT_EQ(std::vector<int>(popped.begin() + 1, popped.end()), expected);
}

// --- the pieces of texture of a node -------------------------------------------------------------

TEST(EqualKeys, piecesOfTextureOfTheSameSizeArePlacedInTheirOrder) {
    // a larger piece, which goes first, and pieces of the same size. A piece is placed after the
    // ones before it in the order, so the first n pieces alone are placed where all of them are.
    std::vector<vcg::Point2i> sizes(60, vcg::Point2i(10, 10));
    sizes[0] = vcg::Point2i(20, 20);
    const vcg::Point2i container(1024, 1024);
    std::vector<vcg::Point2i> all;
    vcg::Point2i covered;
    ASSERT_TRUE(vcg::RectPacker<float>::PackInt(sizes, container, all, covered));
    EXPECT_EQ(all[0], vcg::Point2i(0, 0));

    for (size_t n : {2u, 17u, 31u, 33u, 50u}) {
        const std::vector<vcg::Point2i> first(sizes.begin(), sizes.begin() + n);
        std::vector<vcg::Point2i> positions;
        ASSERT_TRUE(vcg::RectPacker<float>::PackInt(first, container, positions, covered));
        for (size_t i = 0; i < n; i++)
            ASSERT_EQ(positions[i], all[i]) << "piece " << i << " of " << n;
    }
}

// --- the symbols of the entropy coder of corto ---------------------------------------------------

TEST(EqualKeys, symbolsOfTheSameProbabilityAreOrderedBySymbol) {
    // 40 symbols, each 5 times, and one 20 times, in a scrambled order
    std::vector<unsigned char> data;
    for (int i : scrambled(40 * 5))
        data.push_back(static_cast<unsigned char>(10 + i % 40));
    for (int i = 0; i < 20; i++)
        data.push_back(200);
    crt::Tunstall tunstall;
    tunstall.getProbabilities(data.data(), int(data.size()));

    ASSERT_EQ(tunstall.probabilities.size(), 41u);
    EXPECT_EQ(tunstall.probabilities[0].symbol, 200);
    for (int k = 1; k <= 40; k++)
        ASSERT_EQ(tunstall.probabilities[k].symbol, 10 + k - 1) << "at " << k;
}

}  // namespace
