// Standalone smoke test of the very same native engine linked into OBS.
// A constant BGRA frame is not a visual-quality test.
#include "../src/ai-engine.h"
#include "../src/ai-worker.h"
#include "../src/ai-mask-refine.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: native-engine-smoke <model.onnx>\n";
        return 2;
    }
    char error[512] = {0};
    auto *session = camlayer_ai_open(argv[1], error, sizeof(error));
    if (!session) {
        std::cerr << "Failed to initialize native model: " << error << "\n";
        return 1;
    }
    const uint32_t width = 640, height = 360;
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4, 0);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i] = 128; bgra[i + 1] = 128; bgra[i + 2] = 128; bgra[i + 3] = 255;
    }
    std::vector<float> alpha(256u * 256u, -1.0f);
    const int ok = camlayer_ai_segment_bgra(
        session, bgra.data(), width, height, width * 4, alpha.data(), alpha.size());
    camlayer_ai_close(session);
    if (!ok) {
        std::cerr << "Native BGRA segmentation failed\n";
        return 1;
    }
    for (float x : alpha) {
        if (!std::isfinite(x) || x < 0.0f || x > 1.0f) {
            std::cerr << "Invalid alpha value\n";
            return 1;
        }
    }
    // Regression test on synthetic masks, separate from model quality.
    auto refiner = std::make_unique<camlayer::MaskRefiner>();
    std::vector<float> synthetic(camlayer::kPixels, 0.0f);
    const int center = 128 * 256 + 128;
    synthetic[center] = 1.0f;
    refiner->process(synthetic.data(), 0.0f, 0.0f, 1.0f, 1000000000ULL);
    if (synthetic[center] != 1.0f || synthetic[center - 1] != 0.0f) {
        std::cerr << "Disabled controls changed the mask\n";
        return 1;
    }
    refiner->reset();
    refiner->process(synthetic.data(), 1.0f, 0.0f, 1.0f, 1100000000ULL);
    if (!(synthetic[center] < 1.0f)) {
        std::cerr << "Cleanup did not reduce isolated alpha\n";
        return 1;
    }
    refiner->reset();
    std::fill(synthetic.begin(), synthetic.end(), 0.0f);
    synthetic[center] = 1.0f;
    refiner->process(synthetic.data(), 0.0f, 1.0f, 1.0f, 1200000000ULL);
    synthetic[center] = 0.95f;
    refiner->process(synthetic.data(), 0.0f, 1.0f, 1.0f, 1300000000ULL);
    if (!(synthetic[center] > 0.95f && synthetic[center] < 1.0f)) {
        std::cerr << "Stability failed to smooth small changes\n";
        return 1;
    }
    synthetic[center] = 0.0f;
    refiner->process(synthetic.data(), 0.0f, 1.0f, 1.0f, 1400000000ULL);
    if (!(synthetic[center] < 0.1f)) {
        std::cerr << "Motion did not release old silhouette\n";
        return 1;
    }
    // Same stability, different follow speed: fast mode must release
    // more history after a moderate edge movement.
    auto slow = std::make_unique<camlayer::MaskRefiner>();
    auto fast = std::make_unique<camlayer::MaskRefiner>();
    std::vector<float> slow_mask(camlayer::kPixels, 0.0f);
    std::vector<float> fast_mask(camlayer::kPixels, 0.0f);
    slow_mask[center] = fast_mask[center] = 0.70f;
    slow->process(slow_mask.data(), 0.0f, 1.5f, 0.0f, 2000000000ULL);
    fast->process(fast_mask.data(), 0.0f, 1.5f, 2.0f, 2000000000ULL);
    slow_mask[center] = fast_mask[center] = 0.50f;
    slow->process(slow_mask.data(), 0.0f, 1.5f, 0.0f, 2100000000ULL);
    fast->process(fast_mask.data(), 0.0f, 1.5f, 2.0f, 2100000000ULL);
    if (!(fast_mask[center] < slow_mask[center])) {
        std::cerr << "Follow speed did not improve motion response\\n";
        return 1;
    }
    // Reinitializing temporal state must remove residual trails.
    fast->reset();
    fast_mask[center] = 0.20f;
    fast->process(fast_mask.data(), 0.0f, 1.5f, 2.0f, 2200000000ULL);
    if (fast_mask[center] != 0.20f) {
        std::cerr << "Follow speed state reset failed\\n";
        return 1;
    }
    // Browser-style softness: positive expands, negative contracts.
    auto feather = std::make_unique<camlayer::MaskRefiner>();
    std::vector<float> base(camlayer::kPixels, 0.0f);
    for (int y = 100; y < 140; ++y) {
        for (int x = 100; x < 140; ++x)
            base[y * 256 + x] = 1.0f;
    }
    auto neutral = base;
    auto expanded = base;
    auto contracted = base;
    feather->finish_alpha(neutral.data(), 0.5f, 0.0f);
    feather->finish_alpha(expanded.data(), 0.5f, 1.0f);
    feather->finish_alpha(contracted.data(), 0.5f, -1.0f);
    // A modest softness value must generate a genuine translucent
    // transition, not just shift an otherwise binary mask outline.
    auto softly_expanded = base;
    auto softly_contracted = base;
    feather->finish_alpha(softly_expanded.data(), 0.5f, 0.36f);
    feather->finish_alpha(softly_contracted.data(), 0.5f, -0.36f);
    const int outside = 120 * 256 + 99;
    const int inside = 120 * 256 + 100;
    if (!(softly_expanded[outside] > 0.0f && softly_expanded[outside] < 1.0f &&
          softly_contracted[inside] > 0.0f && softly_contracted[inside] < 1.0f)) {
        std::cerr << "Moderate softness did not create partially transparent edges\\n";
        return 1;
    }
    if (!(neutral[outside] == 0.0f && neutral[inside] == 1.0f &&
          expanded[outside] > 0.0f && contracted[inside] < 1.0f)) {
        std::cerr << "Signed softness boundary behavior failed\n";
        return 1;
    }
    // Regression: small slider steps near integer morphology radii
    // must never trigger a sudden edge jump. Test both signs and
    // crossings at radii 1 and 2 (softness = radius / 2.4).
    std::vector<float> near_before(camlayer::kPixels);
    std::vector<float> near_after(camlayer::kPixels);
    for (float sign : {-1.0f, 1.0f}) {
        for (float radius : {1.0f, 2.0f, 3.0f}) {
            near_before = base;
            near_after = base;
            const float boundary = sign * radius / 2.4f;
            const float offset = sign * 0.001f;
            feather->finish_alpha(near_before.data(), 0.5f, boundary - offset);
            feather->finish_alpha(near_after.data(), 0.5f, boundary + offset);
            float largest_step = 0.0f;
            for (size_t i = 0; i < near_before.size(); ++i)
                largest_step = std::max(largest_step,
                    std::fabs(near_before[i] - near_after[i]));
            if (!(largest_step < 0.03f)) {
                std::cerr << "Edge Softness jumps at an integer radius\n";
                return 1;
            }
        }
    }
    auto max_speed = std::make_unique<camlayer::MaskRefiner>();
    std::vector<float> max_mask(camlayer::kPixels, 0.0f);
    max_mask[center] = 0.70f;
    max_speed->process(max_mask.data(), 0.0f, 1.5f, 5.0f, 2000000000ULL);
    max_mask[center] = 0.50f;
    max_speed->process(max_mask.data(), 0.0f, 1.5f, 5.0f, 2100000000ULL);
    if (!(max_mask[center] <= slow_mask[center])) {
        std::cerr << "Follow speed 5 did not release history promptly\n";
        return 1;
    }
    // Confirm the same model can run outside the caller's thread.
    camlayer_worker *worker = camlayer_worker_create(CAMLAYER_BACKEND_MEDIAPIPE, argv[1], 256);
    if (!worker) return 1;
    int state = CAMLAYER_WORKER_STARTING;
    for (int i = 0; i < 200 && state == CAMLAYER_WORKER_STARTING; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        state = camlayer_worker_get_status(worker);
    }
    if (state != CAMLAYER_WORKER_READY ||
        !camlayer_worker_submit(worker, bgra.data(), width, height, width * 4, 12345)) {
        std::cerr << "Worker initialization or frame submission failed\n";
        camlayer_worker_destroy(worker);
        return 1;
    }
    uint64_t timestamp = 0;
    bool received = false;
    for (int i = 0; i < 200; ++i) {
        if (camlayer_worker_take_mask(worker, alpha.data(), alpha.size(), &timestamp)) {
            received = true;
            break;
        }
        if (camlayer_worker_get_status(worker) != CAMLAYER_WORKER_READY) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    camlayer_worker_destroy(worker);
    if (!received || timestamp != 12345) {
        std::cerr << "Async worker did not publish a valid timestamped mask\n";
        return 1;
    }
    // RVM requires a valid official model path and is tested separately
    // with recurrent inference in rvm-engine-smoke.cpp.
    worker = camlayer_worker_create(CAMLAYER_BACKEND_RVM, nullptr, 512);
    if (worker) {
        std::cerr << "Missing RVM model path must not create a worker\n";
        camlayer_worker_destroy(worker);
        return 1;
    }
    std::cout << "CamLayer OBS native engine API smoke test: PASS\n";
    return 0;
}
