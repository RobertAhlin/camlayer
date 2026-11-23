// CamLayer - native ONNX segmentation engine.
// Copyright (C) 2026 Robert Ahlin and CamLayer contributors.
// SPDX-License-Identifier: GPL-3.0-only
#include "ai-engine.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct camlayer_ai_session {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "CamLayer"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> model;
    std::string input_name;
    std::string output_name;
    std::vector<float> rgb;
    std::array<int64_t, 4> shape{1, 3, 256, 256};

    explicit camlayer_ai_session(const std::wstring &path) {
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        model = std::make_unique<Ort::Session>(env, path.c_str(), options);
        if (model->GetInputCount() != 1 || model->GetOutputCount() < 1)
            throw std::runtime_error("Unexpected model inputs or outputs");
        const auto dims = model->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (dims.size() != 4 || (dims[0] != 1 && dims[0] != -1) ||
            dims[1] != 3 || dims[2] != 256 || dims[3] != 256)
            throw std::runtime_error("Expected NCHW 1x3x256x256 model input");
        Ort::AllocatorWithDefaultOptions alloc;
        input_name = model->GetInputNameAllocated(0, alloc).get();
        output_name = model->GetOutputNameAllocated(0, alloc).get();
        rgb.resize(3u * 256u * 256u);
    }
};

static void set_error(char *buffer, size_t size, const char *message) noexcept {
    if (!buffer || !size) return;
    const size_t n = std::min(size - 1, std::strlen(message));
    std::memcpy(buffer, message, n);
    buffer[n] = 0;
}

extern "C" camlayer_ai_session *camlayer_ai_open(
    const char *path, char *error, size_t error_size) {
    try {
        if (!path || !*path) throw std::runtime_error("Missing model file");
#ifdef _WIN32
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
        if (length <= 1) throw std::runtime_error("Invalid UTF-8 model path");
        std::wstring wide(static_cast<size_t>(length), L'\0');
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide.data(), length))
            throw std::runtime_error("Model path conversion failed");
#else
        std::wstring wide(path, path + std::strlen(path));
#endif
        auto *session = new camlayer_ai_session(wide);
        set_error(error, error_size, "");
        return session;
    } catch (const std::exception &e) {
        set_error(error, error_size, e.what());
    } catch (...) {
        set_error(error, error_size, "Unknown ONNX initialization failure");
    }
    return nullptr;
}

extern "C" void camlayer_ai_close(camlayer_ai_session *session) {
    delete session;
}

extern "C" int camlayer_ai_segment_bgra(
    camlayer_ai_session *session, const uint8_t *bgra,
    uint32_t width, uint32_t height, uint32_t stride,
    float *alpha, size_t capacity) {
    if (!session || !bgra || !alpha || capacity < 256u * 256u ||
        width == 0 || height == 0 || width > 8192 || height > 8192 ||
        stride < static_cast<uint64_t>(width) * 4u)
        return 0;
    try {
        const size_t plane = 256u * 256u;
        for (uint32_t y = 0; y < 256; ++y) {
            const uint32_t sy = std::min(height - 1, static_cast<uint32_t>(
                (static_cast<uint64_t>(y) * height) / 256));
            const uint8_t *row = bgra + static_cast<size_t>(sy) * stride;
            for (uint32_t x = 0; x < 256; ++x) {
                const uint32_t sx = std::min(width - 1, static_cast<uint32_t>(
                    (static_cast<uint64_t>(x) * width) / 256));
                const uint8_t *p = row + static_cast<size_t>(sx) * 4;
                const size_t i = static_cast<size_t>(y) * 256 + x;
                session->rgb[i] = p[2] / 255.0f; // red
                session->rgb[plane + i] = p[1] / 255.0f; // green
                session->rgb[2 * plane + i] = p[0] / 255.0f; // blue
            }
        }
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(
            mem, session->rgb.data(), session->rgb.size(),
            session->shape.data(), session->shape.size());
        const char *inputs[] = {session->input_name.c_str()};
        const char *outputs[] = {session->output_name.c_str()};
        auto result = session->model->Run(
            Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
        if (result.size() != 1 || !result[0].IsTensor()) return 0;
        auto info = result[0].GetTensorTypeAndShapeInfo();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            info.GetElementCount() != plane)
            return 0;
        const auto *mask = result[0].GetTensorData<float>();
        for (size_t i = 0; i < plane; ++i) {
            if (!std::isfinite(mask[i])) return 0;
            alpha[i] = std::clamp(mask[i], 0.0f, 1.0f);
        }
        return 1;
    } catch (...) {
        return 0;
    }
}
