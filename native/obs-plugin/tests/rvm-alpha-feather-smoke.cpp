// Standalone regression tests for continuous-alpha RVM Gaussian feather.
#include "../src/rvm-alpha-feather.h"
#include <vector>
#include <memory>
#include <cmath>
#include <cstdio>

int main()
{
    using camlayer::RvmAlphaFeather;
    // Windows GitHub runners use a small default stack. Allocate large masks
    // and the feather working buffer on the heap, like the plugin worker.
    auto feather = std::make_unique<RvmAlphaFeather>();
    std::vector<float> matte(RvmAlphaFeather::pixels);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            matte[y*256+x] = x < 128 ? 1.0f : 0.0f;
    auto original = matte;
    feather->apply(matte.data(), 0.0f, 6.0f, 0.5f);
    if (matte != original) return 1; // Bypass must be exact.
    feather.apply(matte.data(), 2.0f, 5.0f, 0.5f);
    const int row = 128 * 256;
    if (!(matte[row + 120] > matte[row + 126] &&
          matte[row + 126] > matte[row + 129] &&
          matte[row + 129] > matte[row + 135])) {
        std::puts("FAIL: Gaussian edge must fade smoothly and monotonically.");
        return 2;
    }
    for (float a : matte)
        if (!std::isfinite(a) || a < 0.0f || a > 1.0f) return 3;
    if (matte[row + 90] < 0.99f || matte[row + 165] > 0.01f) return 4;
    // The newly extended softness range must change the actual result,
    // not just the slider. Higher softness should spread the transition.
    std::vector<float> soft5(RvmAlphaFeather::pixels), soft10(RvmAlphaFeather::pixels);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            soft5[y*256+x] = soft10[y*256+x] = x < 128 ? 1.0f : 0.0f;
    feather.apply(soft5.data(), 5.0f, 1.25f, 0.5f);
    feather.apply(soft10.data(), 10.0f, 1.25f, 0.5f);
    if (!(soft10[row + 131] > soft5[row + 131])) {
        std::puts("FAIL: softness >5 must broaden Gaussian transition.");
        return 5;
    }
    std::puts("PASS: RVM alpha feather bypass, smooth falloff, range, extended softness.");
    return 0;
}
