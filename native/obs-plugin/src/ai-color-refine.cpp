// Conservative color-aware refinement around uncertain matte edges.
// SPDX-License-Identifier: GPL-3.0-only
#include "ai-color-refine.h"
#include <algorithm>
#include <cmath>

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
    for (size_t i = 0; i < kPixels; ++i) {
        const float current = alpha[i];
        // Keep the inside of the person and the distant background
        // controlled by the segmentation model, not by similar colors.
        if (!(current > 0.01f && current < 0.99f))
            continue;
        const uint8_t *pixel = bgra + i * 4;
        const float bg = match_color(pixel, config->rgb[CAMLAYER_COLOR_BACKGROUND],
                                      config->tolerance[CAMLAYER_COLOR_BACKGROUND]);
        float fg = 0.0f;
        for (int role = CAMLAYER_COLOR_HAIR; role < CAMLAYER_COLOR_COUNT; ++role)
            fg = std::max(fg, match_color(pixel, config->rgb[role],
                                           config->tolerance[role]));
        // If background and person reference colors are equally plausible,
        // abstain rather than destroying hair or clothing detail.
        const float evidence = fg - bg;
        if (std::fabs(evidence) < 0.05f)
            continue;
        const float boundary_weight = 1.0f - std::fabs(current - 0.5f) * 2.0f;
        const float delta = evidence * boundary_weight * strength * 0.30f;
        alpha[i] = std::clamp(current + delta, 0.0f, 1.0f);
    }
}
