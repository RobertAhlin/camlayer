// Standalone regression tests for continuous-alpha RVM Gaussian feather.
#include "../src/rvm-alpha-feather.h"
#include <array>
#include <cmath>
#include <cstdio>

int main()
{
    using camlayer::RvmAlphaFeather;
    RvmAlphaFeather feather;
    std::array<float, RvmAlphaFeather::pixels> matte{};
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            matte[y*256+x] = x < 128 ? 1.0f : 0.0f;
    auto original = matte;
    feather.apply(matte.data(), 0.0f, 6.0f, 0.5f);
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
    std::puts("PASS: RVM alpha feather bypass, smooth falloff, and range.");
    return 0;
}
