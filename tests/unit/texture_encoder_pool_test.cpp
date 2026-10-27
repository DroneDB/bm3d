// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The node textures are encoded on several threads and written in the order they were made, so that
// the file is the same whatever the number of threads and whichever encoding ends first.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "build/node_texture_store.h"
#include "build/texture_encoder_pool.h"
#include "common/error.h"
#include "temp_dir.h"

namespace {

using namespace bm3d::detail;

Image imageOfSide(int side) {
    Image image(side, 1);
    image.fill(Image::rgb(1, 2, 3));
    return image;
}

// Encodes an image to its width as one byte, and records the threads it ran on
class RecordingEncoder : public ImageEncoder {
public:
    std::vector<unsigned char> encode(const Image& image) const override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            threads_.insert(std::this_thread::get_id());
        }
        return {static_cast<unsigned char>(image.width())};
    }
    size_t threadsUsed() const { return threads_.size(); }
    bool ranOn(std::thread::id id) const { return threads_.count(id) > 0; }

private:
    mutable std::mutex mutex_;
    mutable std::set<std::thread::id> threads_;
};

// The first texture it is given waits until a later one has been encoded: with several threads the
// textures end in the opposite order of the one they were submitted in
class ReversingEncoder : public ImageEncoder {
public:
    std::vector<unsigned char> encode(const Image& image) const override {
        if (image.width() == 1) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!laterDone_.wait_for(lock, std::chrono::seconds(20), [this] { return later_; }))
                throw std::runtime_error("the later texture was never encoded");
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            later_ = true;
            laterDone_.notify_all();
        }
        return {static_cast<unsigned char>(image.width())};
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable laterDone_;
    mutable bool later_ = false;
};

struct Delivered {
    std::vector<std::pair<size_t, unsigned char>> items;
    TextureEncoderPool::Sink sink() {
        return [this](size_t texture, const std::vector<unsigned char>& bytes) {
            ASSERT_EQ(bytes.size(), 1u);
            items.emplace_back(texture, bytes[0]);
        };
    }
};

TEST(TextureEncoderPool, withOneThreadATextureIsEncodedAndDeliveredAtOnce) {
    RecordingEncoder encoder;
    Delivered delivered;
    TextureEncoderPool pool(encoder, 1, delivered.sink());
    pool.submit(0, imageOfSide(5));
    ASSERT_EQ(delivered.items.size(), 1u);
    EXPECT_EQ(delivered.items[0].first, 0u);
    EXPECT_EQ(delivered.items[0].second, 5);
    EXPECT_TRUE(encoder.ranOn(std::this_thread::get_id()));
    EXPECT_EQ(encoder.threadsUsed(), 1u);
}

TEST(TextureEncoderPool, texturesAreDeliveredInTheOrderTheyWereSubmittedWhicheverEndsFirst) {
    ReversingEncoder encoder;
    Delivered delivered;
    TextureEncoderPool pool(encoder, 3, delivered.sink());  // two encoding threads
    pool.submit(10, imageOfSide(1));  // ends last
    pool.submit(11, imageOfSide(2));  // ends first
    pool.finish();
    ASSERT_EQ(delivered.items.size(), 2u);
    EXPECT_EQ(delivered.items[0], std::make_pair(size_t(10), static_cast<unsigned char>(1)));
    EXPECT_EQ(delivered.items[1], std::make_pair(size_t(11), static_cast<unsigned char>(2)));
}

TEST(TextureEncoderPool, theTexturesStillBeingEncodedAreDeliveredByFinishAndNotBefore) {
    RecordingEncoder encoder;
    Delivered delivered;
    TextureEncoderPool pool(encoder, 4, delivered.sink());  // three can be pending
    pool.submit(0, imageOfSide(1));
    pool.submit(1, imageOfSide(2));
    pool.submit(2, imageOfSide(3));
    EXPECT_TRUE(delivered.items.empty());
    pool.finish();
    ASSERT_EQ(delivered.items.size(), 3u);
    for (size_t i = 0; i < 3; i++)
        EXPECT_EQ(delivered.items[i].first, i);
    pool.finish();  // nothing is left: nothing happens
    EXPECT_EQ(delivered.items.size(), 3u);
}

TEST(TextureEncoderPool, theOldestTextureIsDeliveredWhenTheThreadsAreAllBusy) {
    RecordingEncoder encoder;
    Delivered delivered;
    TextureEncoderPool pool(encoder, 3, delivered.sink());  // two can be pending
    pool.submit(0, imageOfSide(1));
    pool.submit(1, imageOfSide(2));
    EXPECT_TRUE(delivered.items.empty());
    pool.submit(2, imageOfSide(3));  // makes room by delivering texture 0
    ASSERT_EQ(delivered.items.size(), 1u);
    EXPECT_EQ(delivered.items[0].first, 0u);
    pool.finish();
    ASSERT_EQ(delivered.items.size(), 3u);
    EXPECT_EQ(delivered.items[2].first, 2u);
}

TEST(TextureEncoderPool, manyTexturesComeOutInOrderWhateverTheNumberOfThreads) {
    for (int threads : {1, 2, 5, 16}) {
        RecordingEncoder encoder;
        Delivered delivered;
        TextureEncoderPool pool(encoder, threads, delivered.sink());
        for (int i = 0; i < 100; i++)
            pool.submit(size_t(i), imageOfSide(1 + i % 200));
        pool.finish();
        ASSERT_EQ(delivered.items.size(), 100u) << threads << " threads";
        for (size_t i = 0; i < 100; i++) {
            ASSERT_EQ(delivered.items[i].first, i) << threads << " threads";
            ASSERT_EQ(delivered.items[i].second, static_cast<unsigned char>(1 + i % 200)) << threads << " threads";
        }
    }
}

TEST(TextureEncoderPool, aFailedEncodingIsThrownWhereTheTextureIsDelivered) {
    class Failing : public ImageEncoder {
    public:
        std::vector<unsigned char> encode(const Image&) const override { throw Error("no WebP today"); }
    } encoder;
    Delivered delivered;
    TextureEncoderPool pool(encoder, 2, delivered.sink());
    pool.submit(0, imageOfSide(1));  // the thread that failed is not the one that gets the error
    EXPECT_THROW(pool.finish(), Error);
    EXPECT_TRUE(delivered.items.empty());
}

TEST(NodeTextureStore, aTextureIsReadBackFromWhereItWasStoredWhateverTheOrderItWasStoredIn) {
    bm3dtest::TempDir dir;
    NodeTextureStore store(bm3dtest::pathToUtf8(dir.file("textures")));
    const size_t first = store.reserve(), second = store.reserve(), third = store.reserve();
    EXPECT_EQ(store.count(), 3u);
    store.store(second, {2, 2, 2, 2});
    store.store(first, {1});
    store.store(third, {});
    std::vector<unsigned char> back;
    store.read(first, back);
    EXPECT_EQ(back, (std::vector<unsigned char>{1}));
    store.read(second, back);
    EXPECT_EQ(back, (std::vector<unsigned char>{2, 2, 2, 2}));
    store.read(third, back);
    EXPECT_TRUE(back.empty());
}

}  // namespace
