// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived from nexusbuilder.h and nexusbuilder.cpp of Nexus (Copyright (C) 2012 Federico Ponchio,
// ISTI - Italian National Research Council - Visual Computing Lab; GPL-2.0-or-later), see PROVENANCE.md.

#ifndef BM3D_TEXTURE_ENCODER_POOL_H
#define BM3D_TEXTURE_ENCODER_POOL_H

#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <utility>
#include <vector>

#include "../io/image/image.h"
#include "../io/image/image_encoder.h"

namespace bm3d::detail {

//Encodes the node textures on several threads and hands them to a sink in the order they were
//submitted, whichever finishes first: the file that comes out is the same whatever the number of
//threads. The thread that submits is one of them, so that `threads` threads are busy in all: it
//builds the nodes, and threads - 1 encode meanwhile; with 1 it encodes the texture itself.
//The pool is used from one thread only. The encoder must outlive it.
class TextureEncoderPool {
public:
	//Called on the thread that submits, with the texture and its encoded bytes
	using Sink = std::function<void(size_t texture, const std::vector<unsigned char> &encoded)>;

	TextureEncoderPool(const ImageEncoder &encoder, int threads, Sink sink);

	//Starts encoding the image, first waiting for the oldest texture still being encoded when
	//threads - 1 of them are. The image shares its pixels with the copy the task holds, and
	//nothing writes them any more.
	void submit(size_t texture, const Image &image);
	//Waits for the textures still being encoded and hands them to the sink
	void finish();

private:
	void deliverOldest();

	const ImageEncoder &encoder_;
	size_t maxPending_;
	Sink sink_;
	std::deque<std::pair<size_t, std::future<std::vector<unsigned char>>>> pending_;
};

} //namespace

#endif // BM3D_TEXTURE_ENCODER_POOL_H
