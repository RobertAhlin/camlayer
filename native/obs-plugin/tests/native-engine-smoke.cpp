// Standalone smoke test of the very same native engine linked into OBS.
// A constant BGRA frame is not a visual-quality test.
#include "../src/ai-engine.h"
#include "../src/ai-worker.h"
#include "../src/ai-mask-refine.h"
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <iostream>
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
    camlayer::MaskRefiner refiner;
    std::vector<float> synthetic(camlayer::kPixels, 0.0f);
    const int center = 128 * 256 + 128;
    synthetic[center] = 1.0f;
    refiner.process(synthetic.data(), 0.0f, 0.0f, 1000000000ULL);
    if (synthetic[center] != 1.0f || synthetic[center - 1] != 0.0f) {
        std::cerr << "Disabled controls changed the mask\n";
        return 1;
    }
    refiner.reset();
    refiner.process(synthetic.data(), 1.0f, 0.0f, 1100000000ULL);
    if (!(synthetic[center] < 1.0f)) {
        std::cerr << "Cleanup did not reduce isolated alpha\n";
        return 1;
    }
    refiner.reset();
    std::fill(synthetic.begin(), synthetic.end(), 0.0f);
    synthetic[center] = 1.0f;
    refiner.process(synthetic.data(), 0.0f, 1.0f, 1200000000ULL);
    synthetic[center] = 0.95f;
    refiner.process(synthetic.data(), 0.0f, 1.0f, 1300000000ULL);
    if (!(synthetic[center] > 0.95f && synthetic[center] < 1.0f)) {
        std::cerr << "Stability failed to smooth small changes\n";
        return 1;
    }
    synthetic[center] = 0.0f;
    refiner.process(synthetic.data(), 0.0f, 1.0f, 1400000000ULL);
    if (!(synthetic[center] < 0.1f)) {
        std::cerr << "Motion did not release old silhouette\n";
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
