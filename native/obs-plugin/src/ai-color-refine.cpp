// Conservative color-aware refinement around uncertain matte edges.
// SPDX-License-Identifier: GPL-3.0-only
#include "ai-color-refine.h"
#include <algorithm>
#include <cmath>
#include <array>

namespace {
constexpr size_t kPixels = 256u * 256u;
constexpr float kRgbDiagonal = 441.67295593f;

float match_color(const uint8_t *bgra, uint32_t rgb, float tolerance)
{
    const float r = static_cast<float>((rgb >> 16) & 0xFF);
    const float g = static_cast<float>((rgb >> 8) & 0xFF);
    const float b = static_cast<float>(rgb & 0xFF);
    const float dr = float(bgra[2]) - r;
    const float dg = float(bgra[1]) - g;
    const float db = float(bgra[0]) - b;
    const float normalized = std::sqrt(dr * dr + dg * dg + db * db) / kRgbDiagonal;
    // A user tolerance of zero still permits small camera noise; 100
    // allows a broad family of colors but does not treat all colors as equal.
    const float radius = 0.025f + std::clamp(tolerance, 0.0f, 100.0f) * 0.006f;
    return std::clamp(1.0f - normalized / radius, 0.0f, 1.0f);
}
} // namespace

extern "C" void camlayer_color_refine(
    float *alpha, size_t alpha_capacity, const uint8_t *bgra,
    size_t bgra_capacity, const camlayer_color_config *config)
{
    if (!alpha || !bgra || !config || !config->enabled ||
        alpha_capacity < kPixels || bgra_capacity < kPixels * 4)
        return;
    const float strength = std::clamp(config->strength, 0.0f, 100.0f) * 0.01f;
    if (strength <= 0.0f)
        return;
    // Evaluate only a thin band around the geometric AI edge, including
    // currently opaque/transparent pixels adjacent to a different class.
    // This permits removing green screen bleed where the matte is locally
    // overconfident, without acting as a full-frame chroma key.
    constexpr int kSide = 256;
    constexpr int kRadius = 2;
    // Read immutable source alpha to avoid scan-order-dependent results.
    // This buffer is reused on each inference thread without per-frame
    // allocations once initialized.
    thread_local std::array<float, kPixels> source;
    std::copy_n(alpha, kPixels, source.data());
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const size_t i = size_t(y) * kSide + x;
            const float current = std::clamp(source[i], 0.0f, 1.0f);
            float minimum = current;
            float maximum = current;
            for (int dy = -kRadius; dy <= kRadius; ++dy) {
                const int yy = std::clamp(y + dy, 0, kSide - 1);
                for (int dx = -kRadius; dx <= kRadius; ++dx) {
                    const int xx = std::clamp(x + dx, 0, kSide - 1);
                    const float sample = source[size_t(yy) * kSide + xx];
                    minimum = std::min(minimum, sample);
                    maximum = std::max(maximum, sample);
                }
            }
            // A nearby boundary is required even for a 0 or 1 alpha pixel.
            // Skip flat foreground/background surfaces entirely.
            if (maximum - minimum < 0.12f)
                continue;
            const uint8_t *pixel = bgra + i * 4;
            const float bg = match_color(pixel,
                config->rgb[CAMLAYER_COLOR_BACKGROUND],
                config->tolerance[CAMLAYER_COLOR_BACKGROUND]);
            float fg = 0.0f;
            for (int role = CAMLAYER_COLOR_HAIR; role < CAMLAYER_COLOR_COUNT; ++role)
                fg = std::max(fg, match_color(pixel, config->rgb[role],
                                               config->tolerance[role]));

            const float evidence = fg - bg;
            if (std::fabs(evidence) < 0.12f)
                continue; // Foreground/background color ambiguity: abstain.

            // Stronger than the original tiny alpha offset but remains
            // restricted to the edge zone and scaled by user-selected strength.
            const float confidence = std::clamp(
                (std::fabs(evidence) - 0.12f) / 0.88f, 0.0f, 1.0f);
            const float correction = strength * confidence * 0.82f;
            const float target = evidence > 0.0f ? 1.0f : 0.0f;
            alpha[i] = current + (target - current) * correction;
        }
    }
}
