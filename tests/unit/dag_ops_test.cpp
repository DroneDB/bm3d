// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The graph the levels are made into, and the operations on it once they are made: the order of the
// nodes and of the patches that join them, and the errors and spheres that a node takes from its
// children.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "build/dag_ops.h"
#include "build/model_dag.h"
#include "temp_dir.h"

namespace {

using namespace bm3d::detail;

PatchRecord patch(uint32_t node, uint32_t triangleOffset) {
    PatchRecord record;
    record.node = node;
    record.triangleOffset = triangleOffset;
    return record;
}

Node nodeAt(float x, float radius) {
    Node node;
    node.sphere = vcg::Sphere3f(vcg::Point3f(x, 0, 0), radius);
    return node;
}

// The graph as the levels leave it: the sink, then two leaves (A and B) that point to it, then their
// parent P, which points to both
struct ThreeNodes {
    bm3dtest::TempDir dir;
    ModelDag dag{VertexLayout(), bm3dtest::pathToUtf8(dir.file("chunks"))};
    uint32_t sink = 0, a = 0, b = 0, p = 0;

    ThreeNodes(float errorOfLeaves = 1.0f, float errorOfParent = 2.0f) {
        const NodeBox box;
        a = dag.addNode(nodeAt(10, 1), 0, errorOfLeaves, {patch(0, 10)}, box);
        b = dag.addNode(nodeAt(-10, 1), 1, errorOfLeaves, {patch(0, 10)}, box);
        p = dag.addNode(nodeAt(0, 1), 2, errorOfParent, {patch(a, 5), patch(b, 10)}, box);
    }
};

TEST(ModelDag, startsWithTheSinkAndAddsNodesAfterIt) {
    ThreeNodes g;
    EXPECT_EQ(g.sink, 0u);
    EXPECT_EQ(g.a, 1u);
    EXPECT_EQ(g.b, 2u);
    EXPECT_EQ(g.p, 3u);
    EXPECT_EQ(g.dag.nodeCount(), 4u);
    EXPECT_EQ(g.dag.sink(), 3u);  // the last node, once the order is turned round, is the sink
    EXPECT_EQ(g.dag.boxes().size(), 3u);
}

TEST(ModelDag, aNodeKeepsItsChunkErrorAndTheFirstPatchItIsGiven) {
    ThreeNodes g;
    const std::vector<Node>& nodes = g.dag.nodes();
    EXPECT_EQ(nodes[g.b].chunk, 1u);
    EXPECT_EQ(nodes[g.b].error, 1.0f);
    EXPECT_EQ(nodes[g.a].first_patch, 0u);
    EXPECT_EQ(nodes[g.b].first_patch, 1u);
    EXPECT_EQ(nodes[g.p].first_patch, 2u);
}

TEST(ModelDag, thePatchesOfANodeAreStoredInReverse) {
    ThreeNodes g;
    const std::vector<PatchRecord>& patches = g.dag.patches();
    ASSERT_EQ(patches.size(), 4u);
    EXPECT_EQ(patches[2].node, g.b);
    EXPECT_EQ(patches[3].node, g.a);
}

TEST(DagOps, reverseTurnsTheNodesAndThePatchesRoundAndRenumbersWhatPointsToThem) {
    ThreeNodes g;
    DagOps(g.dag).reverse();
    const std::vector<Node>& nodes = g.dag.nodes();
    const std::vector<PatchRecord>& patches = g.dag.patches();
    // P first and the sink last; the chunk is the mark of the node
    ASSERT_EQ(nodes.size(), 4u);
    EXPECT_EQ(nodes[0].chunk, 2u);   // P
    EXPECT_EQ(nodes[1].chunk, 1u);   // B
    EXPECT_EQ(nodes[2].chunk, 0u);   // A
    // the patches of each node: P to A then B, the leaves to the sink (now node 3)
    EXPECT_EQ(nodes[0].first_patch, 0u);
    EXPECT_EQ(nodes[1].first_patch, 2u);
    EXPECT_EQ(nodes[2].first_patch, 3u);
    EXPECT_EQ(nodes[3].first_patch, 4u);
    ASSERT_EQ(patches.size(), 4u);
    EXPECT_EQ(patches[0].node, 2u);
    EXPECT_EQ(patches[0].triangleOffset, 5u);
    EXPECT_EQ(patches[1].node, 1u);
    EXPECT_EQ(patches[1].triangleOffset, 10u);
    EXPECT_EQ(patches[2].node, 3u);
    EXPECT_EQ(patches[3].node, 3u);
}

TEST(DagOps, saturateRaisesTheErrorOfAParentThatIsNotLargerThanItsChildren) {
    ThreeNodes g(/*leaves*/ 2.0f, /*parent*/ 1.0f);
    DagOps ops(g.dag);
    ops.reverse();
    ops.saturate();
    EXPECT_FLOAT_EQ(g.dag.nodes()[0].error, 2.0f * 1.01f);
    EXPECT_EQ(g.dag.nodes().back().error, 0.0f);  // the sink
}

TEST(DagOps, saturateKeepsTheErrorOfAParentThatIsLargerThanItsChildren) {
    ThreeNodes g(/*leaves*/ 1.0f, /*parent*/ 5.0f);
    DagOps ops(g.dag);
    ops.reverse();
    ops.saturate();
    EXPECT_EQ(g.dag.nodes()[0].error, 5.0f);
}

TEST(DagOps, saturateMakesTheSphereOfAParentContainThoseOfItsChildren) {
    ThreeNodes g;
    DagOps ops(g.dag);
    ops.reverse();
    ops.saturate();
    const Node& parent = g.dag.nodes()[0];
    // the children are 10 away with a radius of 1: 11, and a margin of one percent
    EXPECT_FLOAT_EQ(parent.sphere.Radius(), 11.0f * 1.01f);
    EXPECT_TRUE(parent.sphere.IsIn(g.dag.nodes()[1].sphere));
    EXPECT_TRUE(parent.sphere.IsIn(g.dag.nodes()[2].sphere));
}

TEST(NVertex, verticesAtOnePointAreOrderedWithTheLeafestNodeFirstThenByIndex) {
    vcg::Point3s normal;
    const vcg::Point3f point(1, 2, 3);
    NVertex low(1, 7, point, &normal), high(2, 9, point, &normal), same(2, 3, point, &normal);
    EXPECT_TRUE(high < low);
    EXPECT_TRUE(same < high);
    EXPECT_FALSE(low < high);
    EXPECT_TRUE(NVertex(5, 0, vcg::Point3f(0, 0, 0), &normal) < NVertex(1, 0, point, &normal));
}

}  // namespace
