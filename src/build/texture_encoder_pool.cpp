// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#include "texture_encoder_pool.h"

namespace bm3d::detail {

TextureEncoderPool::TextureEncoderPool(const ImageEncoder &encoder, int threads, Sink sink):
	encoder_(encoder), maxPending_(threads > 1 ? size_t(threads - 1) : 0), sink_(std::move(sink)) {
}

void TextureEncoderPool::submit(size_t texture, const Image &image) {
	if(maxPending_ == 0) {
		sink_(texture, encoder_.encode(image));
		return;
	}
	//the building thread is one of the threads
	while(pending_.size() >= maxPending_)
		deliverOldest();
	const ImageEncoder *encoder = &encoder_;
	pending_.emplace_back(texture, std::async(std::launch::async, [encoder, image]() { return encoder->encode(image); }));
}

void TextureEncoderPool::deliverOldest() {
	const size_t texture = pending_.front().first;
	const std::vector<unsigned char> encoded = pending_.front().second.get();
	pending_.pop_front();
	sink_(texture, encoded);
}

void TextureEncoderPool::finish() {
	while(!pending_.empty())
		deliverOldest();
}

} //namespace
