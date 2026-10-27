// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What to do when a build fails in a way the input can be built round: the settings of the next try.

#ifndef BM3D_FALLBACK_POLICY_H
#define BM3D_FALLBACK_POLICY_H

#include <optional>
#include <string>
#include <vector>

#include "../common/error.h"

namespace bm3d::detail {

//What one try at building a model differs in from the next
struct Attempt {
	int threads = 1;
	int nodeFaces = 0;           //triangles per node
	bool withTextures = true;
	int smallerNodeAttempts = 0; //how many times the nodes were made smaller
};

//Decides, from the way a try failed, whether another is worth making and how. It holds no state of
//the build: the same failure of the same try always gives the same answer.
//  - A node needs a texture too large to pack: the model is built without textures. Models of
//    this kind always built that way.
//  - A node has more vertices than a node holds (a fragmented texture parametrization): the
//    model is built with half as many triangles per node, up to a number of times. Leaving the node
//    out, which Nexus did, leaves a hole in the model.
class FallbackPolicy {
public:
	//first: the try that starts the build. maxSmallerNodeAttempts: how many times the nodes may be
	//made smaller, each time with half as many triangles as the time before
	explicit FallbackPolicy(const Attempt &first, int maxSmallerNodeAttempts = 3);

	//The next try and the warning that tells why it is made
	struct Retry {
		Attempt attempt;
		std::string warning;
	};

	//Nothing when the try was already without textures: the failure stands
	std::optional<Retry> after(const Attempt &failed, const TexturePackingError &error) const;
	//Throws an Error that says the nodes were made as small as allowed when they cannot be made smaller
	Retry after(const Attempt &failed, const NodeTooLargeError &error) const;

	//The warnings that describe a build that succeeded with this try, if it differs from the first
	std::vector<std::string> summary(const Attempt &succeeded) const;

private:
	Attempt first_;
	int maxSmallerNodeAttempts_;
};

} //namespace

#endif // BM3D_FALLBACK_POLICY_H
