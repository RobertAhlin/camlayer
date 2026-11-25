// Native RVM MobileNetV3 ONNX FP32 inference with persistent ConvGRU states.
// SPDX-License-Identifier: GPL-3.0-only
#include "rvm-engine.h"
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

namespace {
constexpr size_t kPixels = 256u * 256u;
constexpr std::array<int64_t, 4> kFrameShape{1, 3, 256, 256};
constexpr std::array<int64_t, 4> kEmptyStateShape{1, 1, 1, 1};
constexpr std::array<int64_t, 1> kRatioShape{1};
constexpr std::array<const char *, 6> kInputs{
    "src", "r1i", "r2i", "r3i", "r4i", "downsample_ratio"};
constexpr std::array<const char *, 6> kOutputs{
    "fgr", "pha", "r1o", "r2o", "r3o", "r4o"};

void copy_error(char *destination, size_t capacity, const char *message) noexcept
{
    if (!destination || !capacity) return;
    const size_t count = std::min(capacity - 1, std::strlen(message));
    std::memcpy(destination, message, count);
    destination[count] = 0;
}
std::wstring convert_path(const char *path)
{
    if (!path || !*path) throw std::runtime_error("Missing RVM model path");
#ifdef _WIN32
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          path, -1, nullptr, 0);
    if (count <= 1) throw std::runtime_error("Invalid RVM UTF-8 model path");
    std::wstring wide(size_t(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             path, -1, wide.data(), count))
        throw std::runtime_error("RVM model path conversion failed");
    return wide;
#else
    return std::wstring(path, path + std::strlen(path));
#endif
}
}

struct camlayer_rvm_session {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "CamLayer-RVM"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> model;
    Ort::MemoryInfo memory{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
    std::array<std::vector<float>, 4> recurrent;
    std::array<std::array<int64_t, 4>, 4> state_shapes{};
    std::vector<float> rgb;
    float ratio = 0.5f;

    explicit camlayer_rvm_session(const std::wstring &path)
    {
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        model = std::make_unique<Ort::Session>(env, path.c_str(), options);
        if (model->GetInputCount() != 6 || model->GetOutputCount() != 6)
            throw std::runtime_error("RVM must have six inputs and six outputs");
        Ort::AllocatorWithDefaultOptions allocator;
        for (size_t i = 0; i < kInputs.size(); ++i) {
            auto name = model->GetInputNameAllocated(i, allocator);
            if (!name || std::strcmp(name.get(), kInputs[i]) != 0)
                throw std::runtime_error("RVM ONNX unexpected input order");
        }
        for (size_t i = 0; i < kOutputs.size(); ++i) {
            auto name = model->GetOutputNameAllocated(i, allocator);
            if (!name || std::strcmp(name.get(), kOutputs[i]) != 0)
                throw std::runtime_error("RVM ONNX unexpected output order");
        }
        auto tensor = model->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        if (tensor.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("RVM requires FP32 ONNX weights");
        rgb.resize(kPixels * 3);
        reset();
    }
    void reset()
    {
        for (size_t i = 0; i < 4; ++i) { recurrent[i].assign(1, 0.0f); state_shapes[i] = kEmptyStateShape; }
    }
};

extern "C" camlayer_rvm_session *camlayer_rvm_open(
    const char *path, char *error, size_t capacity)
{
    try {
        auto session = std::make_unique<camlayer_rvm_session>(convert_path(path));
        copy_error(error, capacity, "");
        return session.release();
    } catch (const std::exception &e) {
        copy_error(error, capacity, e.what());
    } catch (...) {
        copy_error(error, capacity, "Unknown RVM startup error");
    }
    return nullptr;
}

extern "C" void camlayer_rvm_reset(camlayer_rvm_session *session)
{
    if (session) session->reset();
}

extern "C" void camlayer_rvm_close(camlayer_rvm_session *session)
{
    delete session;
}

extern "C" int camlayer_rvm_segment_bgra(
    camlayer_rvm_session *session, const uint8_t *bgra,
    uint32_t stride, float *alpha, size_t capacity)
{
    if (!session || !bgra || !alpha || stride < 256 * 4 ||
        capacity < kPixels) return 0;
    try {
        for (size_t i = 0; i < kPixels; ++i) {
            const size_t y = i / 256;
            const size_t x = i % 256;
            const uint8_t *pixel = bgra + y * stride + x * 4;
            session->rgb[i] = pixel[2] * (1.0f / 255.0f);
            session->rgb[kPixels + i] = pixel[1] * (1.0f / 255.0f);
            session->rgb[kPixels * 2 + i] = pixel[0] * (1.0f / 255.0f);
        }

        std::array<Ort::Value, 6> inputs{
            Ort::Value{nullptr}, Ort::Value{nullptr}, Ort::Value{nullptr},
            Ort::Value{nullptr}, Ort::Value{nullptr}, Ort::Value{nullptr}};
        inputs[0] = Ort::Value::CreateTensor<float>(
            session->memory, session->rgb.data(), session->rgb.size(),
            kFrameShape.data(), kFrameShape.size());
        for (size_t i = 0; i < 4; ++i) {
            const auto &state = session->recurrent[i];
            std::array<int64_t, 4> shape = kEmptyStateShape;
            // Recurrent tensor dimensions are determined by the ONNX output.
            // Store both shape and float data after each successful inference.
            // Shapes live in the session and persist through the call.
            if (!state.empty() && session->state_shapes[i].size() == 4)
                shape = session->state_shapes[i];
            inputs[i + 1] = Ort::Value::CreateTensor<float>(
                session->memory, session->recurrent[i].data(),
                session->recurrent[i].size(), shape.data(), shape.size());
        }
        inputs[5] = Ort::Value::CreateTensor<float>(
            session->memory, &session->ratio, 1,
            kRatioShape.data(), kRatioShape.size());
        const auto output = session->model->Run(
            Ort::RunOptions{nullptr}, kInputs.data(), inputs.data(),
            inputs.size(), kOutputs.data(), kOutputs.size());
        if (output.size() != 6 || !output[1].IsTensor())
            return 0;
        auto alpha_info = output[1].GetTensorTypeAndShapeInfo();
        if (alpha_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            alpha_info.GetElementCount() != kPixels)
            return 0;
        std::array<std::vector<float>, 4> new_states;
        std::array<std::array<int64_t,4>, 4> new_shapes;
        for (size_t i = 0; i < 4; ++i) {
            if (!output[i + 2].IsTensor()) return 0;
            const auto info = output[i + 2].GetTensorTypeAndShapeInfo();
            auto dims = info.GetShape();
            if (dims.size() != 4 ||
                info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                return 0;
            const size_t count = info.GetElementCount();
            if (!count || count > 32u * 1024u * 1024u) return 0;
            new_shapes[i] = {dims[0], dims[1], dims[2], dims[3]};
            const float *data = output[i + 2].GetTensorData<float>();
            new_states[i].assign(data, data + count);
        }
        const float *mask = output[1].GetTensorData<float>();
        for (size_t i = 0; i < kPixels; ++i) {
            if (!std::isfinite(mask[i])) return 0;
            alpha[i] = std::clamp(mask[i], 0.0f, 1.0f);
        }
        session->recurrent = std::move(new_states);
        session->state_shapes = new_shapes;
        return 1;
    } catch (...) {
        return 0;
    }
}
