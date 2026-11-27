// CamLayer mask refinement, shared between MediaPipe and future RVM.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <vector>
#include <cstdint>

namespace camlayer {
constexpr int kSide = 256;
constexpr int kPixels = kSide * kSide;

struct MaskRefiner {
    std::vector<float> previous{};
    std::array<float, kPixels> scratch{};
    std::array<float, kPixels> morph_row{};
    std::array<float, kPixels> alpha_buffer{};
    int side = 256;
    bool has_previous = false;
    uint64_t last_timestamp = 0;

    void reset() noexcept;
    // cleanup: [0,1], stability: [0,2], follow_speed: [0,5].
    // Only call on the owning inference thread.
    void process(float *mask, float cleanup, float stability,
                 float follow_speed, uint64_t timestamp, int mask_side = 256) noexcept;
    // Browser-style signed morphology after thresholding.
    // Negative softness shrinks foreground, positive expands it.
    void finish_alpha(float *mask, float threshold, float softness) noexcept;
};
} // namespace camlayer
