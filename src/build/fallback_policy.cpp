// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fallback_policy.h"

namespace bm3d::detail {

FallbackPolicy::FallbackPolicy(const Attempt &first, int maxSmallerNodeAttempts):
	first_(first), maxSmallerNodeAttempts_(maxSmallerNodeAttempts) {
}

std::optional<FallbackPolicy::Retry> FallbackPolicy::after(const Attempt &failed, const TexturePackingError &error) const {
	if(!failed.withTextures)
		return std::nullopt;
	Retry retry;
	retry.attempt = failed;
	retry.attempt.withTextures = false;
	retry.warning = error.what();
	return retry;
}

FallbackPolicy::Retry FallbackPolicy::after(const Attempt &failed, const NodeTooLargeError &error) const {
	if(failed.smallerNodeAttempts == maxSmallerNodeAttempts_)
		throw Error(std::string(error.what()) + "; nodes of at most " + std::to_string(failed.nodeFaces) +
		            " triangles, the smallest tried (the build started with " + std::to_string(first_.nodeFaces) +
		            "), are still too large for this model");
	Retry retry;
	retry.attempt = failed;
	retry.attempt.nodeFaces /= 2;
	++retry.attempt.smallerNodeAttempts;
	retry.warning = error.what();
	return retry;
}

std::vector<std::string> FallbackPolicy::summary(const Attempt &succeeded) const {
	std::vector<std::string> warnings;
	if(succeeded.nodeFaces != first_.nodeFaces)
		warnings.push_back("built with nodes of " + std::to_string(succeeded.nodeFaces) + " triangles instead of " +
		                   std::to_string(first_.nodeFaces) +
		                   ": the texture parametrization is too fragmented for larger nodes");
	if(!succeeded.withTextures)
		warnings.push_back("textures could not be packed; built without textures");
	return warnings;
}

} //namespace
