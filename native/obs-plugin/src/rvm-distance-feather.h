// RVM Feather v2: Euclidean signed-distance feather for the 256x256 alpha matte.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace camlayer {
class RvmDistanceFeather {
public:
    static constexpr int side = 256;
    static constexpr int pixels = side * side;

    // width is in mask pixels (256x256); softness 0 disables feather.
    void apply(float *alpha, float softness, float width, float inner_share) noexcept
    {
        if (!alpha || softness <= 0.001f || width <= 0.001f) return;
        softness = std::clamp(softness, 0.0f, 5.0f);
        width = std::clamp(width, 0.0f, 20.0f);
        inner_share = std::clamp(inner_share, 0.0f, 1.0f);
        const float band = std::max(0.01f, width * (1.0f + 0.6f * softness));
        const float inner = std::max(0.01f, band * inner_share);
        const float outer = std::max(0.01f, band * (1.0f - inner_share));
        bool any_inside = false, any_outside = false;
        for (int i = 0; i < pixels; ++i) {
            const bool inside = std::isfinite(alpha[i]) && alpha[i] >= 0.5f;
            occupancy_[i] = inside;
            any_inside |= inside;
            any_outside |= !inside;
        }
        if (!any_inside || !any_outside) return;

        distance_to_class(true, inside_distance_);
        distance_to_class(false, outside_distance_);
        for (int i = 0; i < pixels; ++i) {
            // Positive distance = inside. Half-pixel centers the boundary.
            const float distance = occupancy_[i]
                ? std::sqrt(outside_distance_[i]) - 0.5f
                : 0.5f - std::sqrt(inside_distance_[i]);
            float t = std::clamp((distance + outer) / (inner + outer), 0.0f, 1.0f);
            // Smoothstep has zero slope at both edges of the feather zone.
            alpha[i] = t * t * (3.0f - 2.0f * t);
        }
    }

private:
    // Reused scratch is owned by the worker and allocated off its thread stack.
    std::array<bool, pixels> occupancy_{};
    std::array<float, pixels> inside_distance_{};
    std::array<float, pixels> outside_distance_{};
    std::array<float, pixels> scratch_{};
    std::array<float, side> line_{};
    std::array<float, side> line_out_{};
    std::array<int, side> v_{};
    std::array<float, side + 1> z_{};

    // Exact 1D squared Euclidean distance transform, O(n).
    void edt_line() noexcept
    {
        int k = 0;
        v_[0] = 0;
        z_[0] = -1.0e20f;
        z_[1] = 1.0e20f;
        for (int q = 1; q < side; ++q) {
            float s;
            do {
                const int p = v_[k];
                s = ((line_[q] + float(q*q)) - (line_[p] + float(p*p))) /
                    (2.0f * float(q-p));
                if (s <= z_[k]) --k;
                else break;
            } while (k >= 0);
            ++k;
            v_[k] = q;
            z_[k] = s;
            z_[k+1] = 1.0e20f;
        }
        k = 0;
        for (int q = 0; q < side; ++q) {
            while (z_[k+1] < q) ++k;
            const int d = q - v_[k];
            line_out_[q] = float(d*d) + line_[v_[k]];
        }
    }

    void distance_to_class(bool target_inside, std::array<float,pixels> &out) noexcept
    {
        constexpr float far = 1.0e8f;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x)
                line_[x] = occupancy_[y*side+x] == target_inside ? 0.0f : far;
            edt_line();
            for (int x = 0; x < side; ++x)
                scratch_[y*side+x] = line_out_[x];
        }
        for (int x = 0; x < side; ++x) {
            for (int y = 0; y < side; ++y) line_[y] = scratch_[y*side+x];
            edt_line();
            for (int y = 0; y < side; ++y) out[y*side+x] = line_out_[y];
        }
    }
};
} // namespace camlayer
