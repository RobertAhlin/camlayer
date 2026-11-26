// RVM feather v3: continuous-alpha separable Gaussian, no binary/SDF edge.
// Copyright (C) 2026 CamLayer contributors.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace camlayer {
class RvmAlphaFeather {
public:
    static constexpr int side = 256;
    static constexpr int pixels = side * side;
    static constexpr int max_radius = 64;

    // width is in 256x256 mask pixels. Softness 0 bypasses feather entirely.
    // Preserve fractional RVM opacity instead of thresholding a silhouette.
    void apply(float *alpha, float softness, float width, float inner_share) noexcept
    {
        if (!alpha || softness <= 0.001f || width <= 0.001f) return;
        softness = std::clamp(softness, 0.0f, 10.0f);
        width = std::clamp(width, 0.0f, 20.0f);
        inner_share = std::clamp(inner_share, 0.0f, 1.0f);

        // Width sets spatial reach; softness increases the actual Gaussian
        // sigma instead of merely blending a blurred matte with the original.
        // This eliminates the sharp inner contour caused by alpha mixing.
        const float sigma = std::clamp(width * (0.12f + 0.19f * softness),
                                       0.05f, 18.0f);
        const int radius = std::min(max_radius,
                                    static_cast<int>(std::ceil(3.5f * sigma)));
        float weights[max_radius + 1] = {};
        float sum = 0.0f;
        for (int k = 0; k <= radius; ++k) {
            const float d = float(k) / sigma;
            weights[k] = std::exp(-0.5f * d * d);
            sum += k == 0 ? weights[k] : 2.0f * weights[k];
        }
        for (int k = 0; k <= radius; ++k) weights[k] /= sum;

        // Horizontal Gaussian convolution, edges replicate nearest mask pixel.
        for (int y = 0; y < side; ++y) {
            const int row = y * side;
            for (int x = 0; x < side; ++x) {
                float total = weights[0] * safe_alpha(alpha[row + x]);
                for (int k = 1; k <= radius; ++k) {
                    total += weights[k] *
                        (safe_alpha(alpha[row + std::max(0, x-k)]) +
                         safe_alpha(alpha[row + std::min(side-1, x+k)]));
                }
                horizontal_[row + x] = total;
            }
        }

        // Vertical pass. Gaussian tails decay continuously to opaque/clear
        // without an artificial hard boundary on either side of the feather.
        for (int y = 0; y < side; ++y) {
            const int row = y * side;
            for (int x = 0; x < side; ++x) {
                float total = weights[0] * horizontal_[row + x];
                for (int k = 1; k <= radius; ++k) {
                    total += weights[k] *
                        (horizontal_[std::max(0, y-k) * side + x] +
                         horizontal_[std::min(side-1, y+k) * side + x]);
                }
                // Adjust which side of the soft region dominates, without
                // introducing any binary threshold or linear alpha remap.
                // 0.5 is symmetric; >0.5 favours inward feather.
                total = std::clamp(total, 0.0f, 1.0f);
                if (inner_share != 0.5f && total > 0.00001f && total < 0.99999f) {
                    const float bias = (0.5f - inner_share) * 2.0f;
                    const float odds = std::log(total / (1.0f - total)) + bias;
                    total = 1.0f / (1.0f + std::exp(-odds));
                }
                alpha[row + x] = total;
            }
        }
    }

private:
    std::array<float, pixels> horizontal_{};

    static float safe_alpha(float value) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
    }
};
} // namespace camlayer
