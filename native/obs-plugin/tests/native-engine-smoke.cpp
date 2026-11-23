// Standalone smoke test of the very same native engine linked into OBS.
// A constant BGRA frame is not a visual-quality test.
#include "../src/ai-engine.h"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: native-engine-smoke <model.onnx>\n";
        return 2;
    }
    char error[512] = {0};
    auto *session = camlayer_ai_open(argv[1], error, sizeof(error));
    if (!session) {
        std::cerr << "Failed to initialize native model: " << error << "\n";
        return 1;
    }
    const uint32_t width = 640, height = 360;
    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4, 0);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i] = 128; bgra[i + 1] = 128; bgra[i + 2] = 128; bgra[i + 3] = 255;
    }
    std::vector<float> alpha(256u * 256u, -1.0f);
    const int ok = camlayer_ai_segment_bgra(
        session, bgra.data(), width, height, width * 4, alpha.data(), alpha.size());
    camlayer_ai_close(session);
    if (!ok) {
        std::cerr << "Native BGRA segmentation failed\n";
        return 1;
    }
    for (float x : alpha) {
        if (!std::isfinite(x) || x < 0.0f || x > 1.0f) {
            std::cerr << "Invalid alpha value\n";
            return 1;
        }
    }
    std::cout << "CamLayer OBS native engine API smoke test: PASS\n";
    return 0;
}
