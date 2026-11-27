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
                          float follow_speed, uint64_t timestamp, int mask_side) noexcept {
    if (!mask || mask_side < 1 || mask_side > 1024) return;
    if (side != mask_side) {
        side = mask_side;
        previous.resize(size_t(side) * side);
        scratch.resize(size_t(side) * side);
        morph_row.resize(size_t(side) * side);
        alpha_buffer.resize(size_t(side) * side);
        reset();
    }
    // RVM can pass expanded ranges. MediaPipe's controls remain capped by
    // ai-worker.cpp. Keep smoothing bounded to avoid negative alpha weights.
    cleanup = std::clamp(cleanup, 0.0f, 2.0f);
    stability = std::clamp(stability, 0.0f, 4.0f);
    follow_speed = std::clamp(follow_speed, 0.0f, 10.0f);
    // Preserve exact original values when cleanup is zero.
    if (cleanup > 0.0f) {
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const int i = y * side + x;
                const float center = std::clamp(mask[i], 0.0f, 1.0f);
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    const int yy = std::clamp(y + dy, 0, side - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = std::clamp(x + dx, 0, side - 1);
                        sum += std::clamp(mask[yy * side + xx], 0.0f, 1.0f);
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
        // Preserve the existing response through 2.0, then increase gradually.
        // Never exceed 90% history or invert the current-frame contribution.
        const float history = stability <= 2.0f
            ? 0.35f * stability
            : 0.70f + 0.20f * (1.0f - std::exp(-(stability - 2.0f)));
        for (int i = 0; i < (side * side); ++i) {
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
    for (int i = 0; i < (side * side); ++i)
        previous[i] = std::clamp(mask[i], 0.0f, 1.0f);
    has_previous = true;
    last_timestamp = timestamp;
}

void MaskRefiner::finish_alpha(float *mask, float threshold, float softness) noexcept
{
    if (!mask) return;
    threshold = std::clamp(threshold, 0.15f, 0.85f);
    softness = std::clamp(softness, -2.0f, 2.0f);
    // Match the browser's smoothstep transfer centered on mask threshold.
    const float low = threshold - 0.075f;
    for (int i = 0; i < kPixels; ++i) {
        const float t = std::clamp((mask[i] - low) / 0.15f, 0.0f, 1.0f);
        alpha_buffer[i] = t * t * (3.0f - 2.0f * t);
    }
    const float desired = std::fabs(softness) * 2.4f;
    if (desired <= 0.0f) {
        std::copy(alpha_buffer.begin(), alpha_buffer.end(), mask);
        return;
    }
    const bool expand = softness > 0.0f;
    const int lower = std::min(8, static_cast<int>(std::floor(desired)));
    const int upper = std::min(8, lower + 1);
    const float fraction = std::clamp(desired - float(lower), 0.0f, 1.0f);
    // Interpolate between adjacent morphology radii. Both mask shape
    // and output alpha evolve continuously when softness moves.
    const auto morph = [&](int radius, float *output) {
        if (radius == 0) {
            std::copy(alpha_buffer.begin(), alpha_buffer.end(), output);
            return;
        }
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                float v = expand ? 0.0f : 1.0f;
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int xx = std::clamp(x + dx, 0, kSide - 1);
                    const float p = alpha_buffer[y * kSide + xx];
                    v = expand ? std::max(v, p) : std::min(v, p);
                }
                morph_row[y * kSide + x] = v;
            }
        }
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                float v = expand ? 0.0f : 1.0f;
                for (int dy = -radius; dy <= radius; ++dy) {
                    const int yy = std::clamp(y + dy, 0, kSide - 1);
                    const float p = morph_row[yy * kSide + x];
                    v = expand ? std::max(v, p) : std::min(v, p);
                }
                output[y * kSide + x] = v;
            }
        }
    };
    morph(lower, mask);
    if (fraction > 0.0f && upper != lower) {
        morph(upper, scratch.data());
        for (int i = 0; i < kPixels; ++i)
            mask[i] += fraction * (scratch[i] - mask[i]);
    }

    // Morphology changes the silhouette position, not its edge opacity.
    // Apply a separable Gaussian feather to produce intermediate alpha
    // values around the boundary, as expected from an Edge Softness control.
    // Keep the signed inward/outward displacement above, but use magnitude
    // to control an independent feather radius and blending amount.
    const float sigma = 0.60f + std::fabs(softness) * 1.15f;
    const int feather_radius = 8; // Fixed Gaussian support avoids step changes.
    const float feather_strength = std::min(1.0f, std::fabs(softness) * 1.7f);
    float kernel[9] = {};
    float weight_sum = 0.0f;
    for (int k = 0; k <= feather_radius; ++k) {
        const float distance = static_cast<float>(k);
        kernel[k] = std::exp(-0.5f * distance * distance / (sigma * sigma));
        weight_sum += k == 0 ? kernel[k] : 2.0f * kernel[k];
    }
    for (int k = 0; k <= feather_radius; ++k)
        kernel[k] /= weight_sum;

    // Horizontal convolution to scratch.
    for (int y = 0; y < kSide; ++y) {
        const int row = y * kSide;
        for (int x = 0; x < kSide; ++x) {
            float accum = kernel[0] * mask[row + x];
            for (int k = 1; k <= feather_radius; ++k) {
                const int left = std::max(0, x - k);
                const int right = std::min(kSide - 1, x + k);
                accum += kernel[k] * (mask[row + left] + mask[row + right]);
            }
            scratch[row + x] = accum;
        }
    }
    // Vertical convolution. Read only scratch, blend back against the
    // unblurred signed-morphology result to preserve a meaningful edge.
    for (int y = 0; y < kSide; ++y) {
        const int row = y * kSide;
        for (int x = 0; x < kSide; ++x) {
            float accum = kernel[0] * scratch[row + x];
            for (int k = 1; k <= feather_radius; ++k) {
                const int top = std::max(0, y - k);
                const int bottom = std::min(kSide - 1, y + k);
                accum += kernel[k] *
                    (scratch[top * kSide + x] + scratch[bottom * kSide + x]);
            }
            const int i = row + x;
            mask[i] = std::clamp(mask[i] * (1.0f - feather_strength) +
                                 accum * feather_strength, 0.0f, 1.0f);
        }
    }
}

} // namespace camlayer
