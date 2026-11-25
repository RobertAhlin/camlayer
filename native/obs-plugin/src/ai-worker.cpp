// CamLayer async segmentation worker.
// Copyright (C) 2026 Robert Ahlin and CamLayer contributors.
// SPDX-License-Identifier: GPL-3.0-only
#include "ai-worker.h"
#include "ai-engine.h"
#include "rvm-engine.h"
#include "ai-mask-refine.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <new>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr uint32_t kMaskSide = 256;
constexpr size_t kMaskPixels = size_t(kMaskSide) * kMaskSide;
constexpr size_t kPackedBytes = kMaskPixels * 4;
}

struct camlayer_worker {
    int backend;
    std::string model_path;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool stop = false;
    bool pending = false;
    bool has_result = false;
    int status = CAMLAYER_WORKER_STARTING;
    uint64_t pending_timestamp = 0;
    uint64_t completed_timestamp = 0;
    std::vector<uint8_t> queued;
    std::array<float, kMaskPixels> result{};
    float threshold = 0.50f;
    float softness = 0.0f;
    float cleanup = 0.0f;
    float stability = 0.0f;
    float follow_speed = 1.0f;
    uint64_t reset_epoch = 0;

    camlayer_worker(int engine, const char *path)
        : backend(engine), model_path(path ? path : ""), queued(kPackedBytes) {}
};

static void run_worker(camlayer_worker *worker) noexcept
{
    char error[512] = {};
    camlayer_ai_session *media = nullptr;
    camlayer_rvm_session *rvm = nullptr;
    if (worker->backend == CAMLAYER_BACKEND_MEDIAPIPE)
        media = camlayer_ai_open(worker->model_path.c_str(), error, sizeof(error));
    else if (worker->backend == CAMLAYER_BACKEND_RVM)
        rvm = camlayer_rvm_open(worker->model_path.c_str(), error, sizeof(error));
    if (!media && !rvm) {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->status = CAMLAYER_WORKER_FAILED;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->status = CAMLAYER_WORKER_READY;
    }
    std::vector<uint8_t> local(kPackedBytes);
    std::array<float, kMaskPixels> computed{};
    // Keep the large temporal mask buffers off the small Windows thread stack.
    std::unique_ptr<camlayer::MaskRefiner> refiner(
        new (std::nothrow) camlayer::MaskRefiner());
    if (!refiner) {
        camlayer_ai_close(media);
        camlayer_rvm_close(rvm);
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->status = CAMLAYER_WORKER_FAILED;
        return;
    }
    uint64_t applied_epoch = 0;
    for (;;) {
        uint64_t timestamp = 0;
        float threshold = 0.50f;
        float softness = 0.0f;
        float cleanup = 0.0f;
        float stability = 0.0f;
        float follow_speed = 1.0f;
        uint64_t epoch = 0;
        {
            std::unique_lock<std::mutex> lock(worker->mutex);
            worker->cv.wait(lock, [&] { return worker->stop || worker->pending; });
            if (worker->stop) break;
            local.swap(worker->queued);
            timestamp = worker->pending_timestamp;
            threshold = worker->threshold;
            softness = worker->softness;
            cleanup = worker->cleanup;
            stability = worker->stability;
            follow_speed = worker->follow_speed;
            epoch = worker->reset_epoch;
            worker->pending = false;
        }
        const int ok = media
            ? camlayer_ai_segment_bgra(media, local.data(), kMaskSide, kMaskSide,
                                        kMaskSide * 4, computed.data(), computed.size())
            : camlayer_rvm_segment_bgra(rvm, local.data(), kMaskSide * 4,
                                        computed.data(), computed.size());
        if (ok) {
            if (epoch != applied_epoch) {
                refiner->reset();
                if (rvm) camlayer_rvm_reset(rvm);
                applied_epoch = epoch;
            }
            // The inference thread owns the entire mask processing state.
            // RVM already produces a continuous alpha matte. Preserve it instead of
            // applying MediaPipe's threshold and signed morphological feather.
            // Optional RVM cleanup/stability remain separately adjustable.
            if (rvm) {
                if (cleanup > 0.0f || stability > 0.0f)
                    refiner->process(computed.data(), cleanup, stability,
                                     follow_speed, timestamp);
            } else {
                refiner->process(computed.data(), cleanup, stability, follow_speed, timestamp);
                refiner->finish_alpha(computed.data(), threshold, softness);
            }
            std::lock_guard<std::mutex> lock(worker->mutex);
            // Publish each completed mask even when a newer frame is queued.
            // The one-slot queue remains bounded; skipping all results under
            // heavy load would freeze the visible silhouette indefinitely.
            // Only discard results from a reset/previous camera generation.
            if (!worker->stop && epoch == worker->reset_epoch) {
                worker->result = computed;
                worker->completed_timestamp = timestamp;
                worker->has_result = true;
            }
        } else {
            std::lock_guard<std::mutex> lock(worker->mutex);
            worker->status = CAMLAYER_WORKER_FAILED;
            break;
        }
    }
    camlayer_ai_close(media);
    camlayer_rvm_close(rvm);
}

extern "C" camlayer_worker *camlayer_worker_create(int backend, const char *model_path)
{
    if (backend != CAMLAYER_BACKEND_MEDIAPIPE && backend != CAMLAYER_BACKEND_RVM)
        return nullptr;
    if (!model_path || !*model_path)
        return nullptr;
    try {
        auto *worker = new camlayer_worker(backend, model_path);
        try {
            worker->thread = std::thread(run_worker, worker);
        } catch (...) {
            delete worker;
            throw;
        }
        return worker;
    } catch (...) {
        return nullptr;
    }
}

extern "C" void camlayer_worker_destroy(camlayer_worker *worker)
{
    if (!worker) return;
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        worker->stop = true;
        worker->pending = false;
    }
    worker->cv.notify_one();
    if (worker->thread.joinable()) worker->thread.join();
    delete worker;
}

extern "C" int camlayer_worker_submit(
    camlayer_worker *worker, const uint8_t *bgra, uint32_t width,
    uint32_t height, uint32_t stride, uint64_t timestamp)
{
    if (!worker || !bgra || !width || !height || width > 8192 || height > 8192 ||
        uint64_t(stride) < uint64_t(width) * 4)
        return 0;

    // Shrink in the submitting thread, without model inference or allocating
    // a full-resolution frame. Work is capped to 256x256 pixels.
    std::array<uint8_t, kPackedBytes> reduced;
    for (uint32_t y = 0; y < kMaskSide; ++y) {
        const uint32_t sy = uint32_t(uint64_t(y) * height / kMaskSide);
        const uint8_t *row = bgra + size_t(sy) * stride;
        for (uint32_t x = 0; x < kMaskSide; ++x) {
            const uint32_t sx = uint32_t(uint64_t(x) * width / kMaskSide);
            std::memcpy(reduced.data() + (size_t(y) * kMaskSide + x) * 4,
                        row + size_t(sx) * 4, 4);
        }
    }
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        if (worker->stop || worker->status != CAMLAYER_WORKER_READY) return 0;
        std::memcpy(worker->queued.data(), reduced.data(), kPackedBytes);
        worker->pending_timestamp = timestamp;
        worker->pending = true;
    }
    worker->cv.notify_one();
    return 1;
}

extern "C" int camlayer_worker_take_mask(
    camlayer_worker *worker, float *dst, size_t capacity, uint64_t *timestamp)
{
    if (!worker || !dst || capacity < kMaskPixels) return 0;
    std::lock_guard<std::mutex> lock(worker->mutex);
    if (!worker->has_result) return 0;
    std::memcpy(dst, worker->result.data(), kMaskPixels * sizeof(float));
    if (timestamp) *timestamp = worker->completed_timestamp;
    worker->has_result = false;
    return 1;
}

extern "C" int camlayer_worker_get_status(camlayer_worker *worker)
{
    if (!worker) return CAMLAYER_WORKER_FAILED;
    std::lock_guard<std::mutex> lock(worker->mutex);
    return worker->status;
}

extern "C" void camlayer_worker_set_refinement(
    camlayer_worker *worker, float threshold, float softness,
    float cleanup, float stability, float follow_speed)
{
    if (!worker) return;
    std::lock_guard<std::mutex> lock(worker->mutex);
    worker->threshold = std::clamp(threshold, 0.15f, 0.85f);
    worker->softness = std::clamp(softness, -2.0f, 2.0f);
    worker->cleanup = std::clamp(cleanup, 0.0f, 1.0f);
    worker->stability = std::clamp(stability, 0.0f, 2.0f);
    worker->follow_speed = std::clamp(follow_speed, 0.0f, 5.0f);
}

extern "C" void camlayer_worker_reset_refinement(camlayer_worker *worker)
{
    if (!worker) return;
    std::lock_guard<std::mutex> lock(worker->mutex);
    ++worker->reset_epoch;
    worker->has_result = false;
    worker->pending = false;
}

