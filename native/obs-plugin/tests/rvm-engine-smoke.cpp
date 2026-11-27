// RVM recurrent inference test; run on the GitHub Windows runner.
// SPDX-License-Identifier: GPL-3.0-only
#include "../src/rvm-engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    char error[512] = {};
    camlayer_rvm_session *model = camlayer_rvm_open(argv[1], error, sizeof(error));
    if (!model) {
        std::cerr << "RVM model open failed: " << error << "\n";
        return 1;
    }
    std::vector<uint8_t> bgra(256 * 256 * 4);
    std::vector<float> alpha(256 * 256);
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            const size_t p = (size_t(y) * 256 + x) * 4;
            bgra[p] = static_cast<uint8_t>(x);
            bgra[p + 1] = static_cast<uint8_t>(y);
            bgra[p + 2] = static_cast<uint8_t>((x + y) / 2);
            bgra[p + 3] = 255;
        }
    }
    // Two frames prove all four returned RVM state tensors can feed
    // the next step, rather than merely returning a first-frame alpha.
    for (int frame = 0; frame < 2; ++frame) {
        if (!camlayer_rvm_segment_bgra(model, bgra.data(), 256 * 4,
                                        alpha.data(), alpha.size(), 256)) {
            std::cerr << "RVM ONNX frame " << frame << " failed\n";
            camlayer_rvm_close(model);
            return 1;
        }
        for (const float value : alpha) {
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
                std::cerr << "RVM alpha invalid\n";
                camlayer_rvm_close(model);
                return 1;
            }
        }
    }
    camlayer_rvm_reset(model);
    if (!camlayer_rvm_segment_bgra(model, bgra.data(), 256 * 4,
                                    alpha.data(), alpha.size(), 256)) {
        std::cerr << "RVM failed after state reset\n";
        camlayer_rvm_close(model);
        return 1;
    }
    // Verify that model inference returns an actual higher-resolution matte.
    // Do not upsample a 256 mask and mistake it for additional model detail.
    for (const int side : {384, 512}) {
        std::vector<uint8_t> frame(size_t(side) * side * 4, 128);
        std::vector<float> high_alpha(size_t(side) * side);
        for (size_t i = 3; i < frame.size(); i += 4) frame[i] = 255;
        if (!camlayer_rvm_segment_bgra(model, frame.data(), side * 4,
                                        high_alpha.data(), high_alpha.size(), side)) {
            std::cerr << "RVM native alpha inference failed at " << side << "\n";
            camlayer_rvm_close(model);
            return 1;
        }
        for (float value : high_alpha)
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
                camlayer_rvm_close(model);
                return 1;
            }
    }
    camlayer_rvm_close(model);
    std::cout << "RVM ONNX 256, 384 and 512 recurrent inference: PASS\\n";
    return 0;
}
