// Optional color-guided refinement limited to uncertain alpha boundaries.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum camlayer_color_role {
    CAMLAYER_COLOR_BACKGROUND = 0,
    CAMLAYER_COLOR_HAIR = 1,
    CAMLAYER_COLOR_SKIN = 2,
    CAMLAYER_COLOR_CLOTHING = 3,
    CAMLAYER_COLOR_COUNT = 4
};

typedef struct camlayer_color_config {
    int enabled;
    uint32_t rgb[CAMLAYER_COLOR_COUNT]; /* 0xRRGGBB, OBS color property format */
    float tolerance[CAMLAYER_COLOR_COUNT]; /* 0..100 percent */
    float strength; /* 0..100 percent */
} camlayer_color_config;

/* Pure CPU alpha refinement, independent of OBS and model backend.
 * Modifies only partially transparent edge pixels. Input is 256x256
 * BGRA, tightly packed; neither the camera image nor color config changes.
 */
void camlayer_color_refine(float *alpha, size_t alpha_capacity,
                           const uint8_t *bgra, size_t bgra_capacity,
                           const camlayer_color_config *config);
#ifdef __cplusplus
}
#endif
