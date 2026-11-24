// CamLayer mask cleanup and motion-adaptive temporal stabilization.
// SPDX-License-Identifier: GPL-3.0-only
#include "ai-mask-refine.h"
#include <algorithm>
#include <cmath>
namespace camlayer {
void MaskRefiner::reset() noexcept {
    has_previous = false;
    last_timestamp = 0;
}
void MaskRefiner::process(float *mask, float cleanup, float stability,
                          float follow_speed, uint64_t timestamp) noexcept {
    if (!mask) return;
    cleanup = std::clamp(cleanup, 0.0f, 1.0f);
    stability = std::clamp(stability, 0.0f, 2.0f);
    follow_speed = std::clamp(follow_speed, 0.0f, 2.0f);
    // Preserve exact original values when cleanup is zero.
    if (cleanup > 0.0f) {
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                const int i = y * kSide + x;
                const float center = std::clamp(mask[i], 0.0f, 1.0f);
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int yy = std::clamp(y + dy, 0, kSide - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = std::clamp(x + dx, 0, kSide - 1);
                        sum += std::clamp(mask[yy * kSide + xx], 0.0f, 1.0f);
                    }
                }
                // Only attenuate an outlying alpha value, never expand it
                // into background. Keeps hair and clothing silhouettes intact.
                const float neighborhood = sum / 9.0f;
                const float outlier = std::max(0.0f, center - neighborhood);
                scratch[i] = std::clamp(center - cleanup * outlier * 1.6f,
                                        0.0f, 1.0f);
            }
        }
        std::copy(scratch.begin(), scratch.end(), mask);
    }

    // The first mask and discontinuous timestamps establish a new stream.
    // Time is reported by the worker from the source frame, in nanoseconds.
    const bool continuous = has_previous && timestamp > last_timestamp &&
        (timestamp - last_timestamp) <= 500000000ULL;
    if (stability > 0.0f && continuous) {
        const float history = 0.35f * stability; // at most 70% history
        for (int i = 0; i < kPixels; ++i) {
            const float current = std::clamp(mask[i], 0.0f, 1.0f);
            const float prior = previous[i];
            const float delta = std::fabs(current - prior);
            // Follow speed is independent of stability: higher values
            // release history more aggressively as the current mask changes.
            // Stationary pixels retain stability smoothing at any speed.
            const float motion_gate = std::clamp(
                1.0f - delta * (2.0f + follow_speed * 3.0f), 0.0f, 1.0f);
            mask[i] = current + history * motion_gate * (prior - current);
        }
    }
    for (int i = 0; i < kPixels; ++i)
        previous[i] = std::clamp(mask[i], 0.0f, 1.0f);
    has_previous = true;
    last_timestamp = timestamp;
}
} // namespace camlayer
