// Standalone smoke test of the very same native engine linked into OBS.
// A constant BGRA frame is not a visual-quality test.
#include "../src/ai-engine.h"
#include "../src/ai-worker.h"
#include "../src/ai-mask-refine.h"
#include "../src/ai-color-refine.h"
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
    // Advanced mode must be inert when disabled and conservative for
    // ambiguous pixels; it must distinguish clear foreground/background
    // evidence only at uncertain edges.
    camlayer_color_config colors = {};
    colors.enabled = 0;
    colors.strength = 100.0f;
    colors.rgb[CAMLAYER_COLOR_BACKGROUND] = 0x888888;
    colors.rgb[CAMLAYER_COLOR_HAIR] = 0x554433;
    colors.rgb[CAMLAYER_COLOR_SKIN] = 0xC99D83;
    colors.rgb[CAMLAYER_COLOR_CLOTHING] = 0x303030;
    for (int role = 0; role < CAMLAYER_COLOR_COUNT; ++role)
        colors.tolerance[role] = 25.0f;
    std::vector<float> color_alpha(camlayer::kPixels, 0.5f);
    std::vector<uint8_t> color_frame(camlayer::kPixels * 4, 0);
    for (size_t i = 0; i < color_frame.size(); i += 4) {
        color_frame[i] = 0x88; // BGRA background reference
        color_frame[i + 1] = 0x88;
        color_frame[i + 2] = 0x88;
        color_frame[i + 3] = 0xFF;
    }
    camlayer_color_refine(color_alpha.data(), color_alpha.size(),
                          color_frame.data(), color_frame.size(), &colors);
    if (color_alpha[0] != 0.5f) {
        std::cerr << "Disabled advanced refinement changed the mask\n";
        return 1;
    }
    colors.enabled = 1;
    // The advanced algorithm intentionally changes real boundaries only.
    // Introduce a neighboring opaque pixel to create an AI edge.
    color_alpha[1] = 1.0f;
    camlayer_color_refine(color_alpha.data(), color_alpha.size(),
                          color_frame.data(), color_frame.size(), &colors);
    if (!(color_alpha[0] < 0.5f)) {
        std::cerr << "Background reference failed to reduce edge alpha\n";
        return 1;
    }
    std::fill(color_alpha.begin(), color_alpha.end(), 0.5f);
    color_alpha[1] = 1.0f;
    for (size_t i = 0; i < color_frame.size(); i += 4) {
        color_frame[i] = 0x33;
        color_frame[i + 1] = 0x44;
        color_frame[i + 2] = 0x55;
    }
    camlayer_color_refine(color_alpha.data(), color_alpha.size(),
                          color_frame.data(), color_frame.size(), &colors);
    if (!(color_alpha[0] > 0.5f)) {
        std::cerr << "Foreground reference failed to protect edge alpha\n";
        return 1;
    }
    std::fill(color_alpha.begin(), color_alpha.end(), 0.5f);
    color_alpha[1] = 1.0f;
    colors.rgb[CAMLAYER_COLOR_HAIR] = colors.rgb[CAMLAYER_COLOR_BACKGROUND];
    for (size_t i = 0; i < color_frame.size(); i += 4) {
        color_frame[i] = color_frame[i + 1] = color_frame[i + 2] = 0x88;
    }
    camlayer_color_refine(color_alpha.data(), color_alpha.size(),
                          color_frame.data(), color_frame.size(), &colors);
    if (color_alpha[0] != 0.5f) {
        std::cerr << "Ambiguous matching foreground/background must be neutral\n";
        return 1;
    }
    // Fully opaque person and fully transparent background are immutable.
    color_alpha[0] = 1.0f;
    color_alpha[1] = 0.0f;
    camlayer_color_refine(color_alpha.data(), color_alpha.size(),
                          color_frame.data(), color_frame.size(), &colors);
    if (color_alpha[0] != 1.0f || color_alpha[1] != 0.0f) {
        std::cerr << "Advanced refinement modified confident model pixels\n";
        return 1;
    }
    // Strong background evidence should also repair a hard AI boundary,
    // rather than only changing pixels that were semi-transparent already.
    colors.rgb[CAMLAYER_COLOR_HAIR] = 0x554433;
    std::fill(color_alpha.begin(), color_alpha.end(), 0.0f);
    for (int y = 90; y < 166; ++y) {
        for (int x = 90; x < 166; ++x)
            color_alpha[size_t(y) * 256 + x] = 1.0f;
    }
    const size_t edge_in = size_t(120) * 256 + 90;
    const size_t deep_in = size_t(120) * 256 + 120;
    camlayer_color_refine(color_alpha.data(), color_alpha.size(),
                          color_frame.data(), color_frame.size(), &colors);
    if (!(color_alpha[edge_in] < 0.9f && color_alpha[deep_in] == 1.0f)) {
        std::cerr << "Color correction must affect hard edge, not interior\n";
        return 1;
    }
    // Confirm the same model can run outside the caller's thread.
    camlayer_worker *worker = camlayer_worker_create(CAMLAYER_BACKEND_MEDIAPIPE, argv[1]);
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
    // RVM is represented honestly as unavailable until the backend exists.
    worker = camlayer_worker_create(CAMLAYER_BACKEND_RVM, nullptr);
    if (!worker) return 1;
    for (int i = 0; i < 50 &&
         camlayer_worker_get_status(worker) == CAMLAYER_WORKER_STARTING; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool rvm_unavailable =
        camlayer_worker_get_status(worker) == CAMLAYER_WORKER_UNAVAILABLE;
    camlayer_worker_destroy(worker);
    if (!rvm_unavailable) {
        std::cerr << "RVM placeholder must not report ready\n";
        return 1;
    }
    std::cout << "CamLayer OBS native engine API smoke test: PASS\n";
    return 0;
}
