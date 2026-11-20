// CamLayer native segmentation runtime smoke test.
// This is a command-line probe, not a real-time OBS integration.
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

static std::string describe_shape(const std::vector<int64_t> &shape)
{
    std::string value;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i) value += "x";
        value += std::to_string(shape[i]);
    }
    return value;
}

static size_t checked_element_count(const std::vector<int64_t> &shape)
{
    size_t count = 1;
    if (shape.empty()) throw std::runtime_error("Unsupported scalar tensor.");
    for (const auto dim : shape) {
        if (dim <= 0 || dim > 8192 || count > (32u * 1024u * 1024u) / static_cast<size_t>(dim))
            throw std::runtime_error("Unsupported or dynamic tensor dimension.");
        count *= static_cast<size_t>(dim);
    }
    return count;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: camlayer-model-probe <model.onnx>\n";
        return 2;
    }

    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "CamLayer");
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(2);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
        const int length = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        if (length <= 1) throw std::runtime_error("Invalid model path.");
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, wide.data(), length);
        Ort::Session session(env, wide.c_str(), session_options);
#else
        Ort::Session session(env, argv[1], session_options);
#endif

        const size_t inputs = session.GetInputCount();
        const size_t outputs = session.GetOutputCount();
        if (inputs != 1 || outputs < 1)
            throw std::runtime_error("Expected one input and at least one output.");

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name = session.GetInputNameAllocated(0, allocator);
        auto input_type = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        auto input_shape = input_type.GetShape();
        if (input_type.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("Expected a float32 model input.");
        if (input_shape.size() != 4 || input_shape[0] == -1)
            throw std::runtime_error("Expected a fixed 4D input with batch=1.");

        const size_t elements = checked_element_count(input_shape);
        std::vector<float> image(elements, 0.5f);
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, image.data(), image.size(), input_shape.data(), input_shape.size());

        auto output_name = session.GetOutputNameAllocated(0, allocator);
        const char *input_names[] = {input_name.get()};
        const char *output_names[] = {output_name.get()};
        auto results = session.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1,
                                   output_names, 1);
        if (results.size() != 1 || !results[0].IsTensor())
            throw std::runtime_error("Expected tensor output.");
        const auto output_info = results[0].GetTensorTypeAndShapeInfo();
        if (output_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("Expected float32 mask output.");

        const auto output_shape = output_info.GetShape();
        const auto output_elements = checked_element_count(output_shape);
        const float *mask = results[0].GetTensorData<float>();
        float minimum = mask[0];
        float maximum = mask[0];
        double average = 0;
        for (size_t i = 0; i < output_elements; ++i) {
            const float value = mask[i];
            if (!std::isfinite(value))
                throw std::runtime_error("Model output contains NaN or Inf.");
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            average += value;
        }
        average /= static_cast<double>(output_elements);

        std::cout << "ONNX Runtime: " << Ort::GetApi().GetVersionString() << "\n";
        std::cout << "Input: " << input_name.get() << " [" << describe_shape(input_shape)
                  << "] float32\n";
        std::cout << "Output: " << output_name.get() << " [" << describe_shape(output_shape)
                  << "] float32\n";
        std::cout << "Output stats: min=" << minimum << " max=" << maximum
                  << " mean=" << average << "\n";
        std::cout << "CamLayer native model inference: PASS\n";
        return 0;
    } catch (const Ort::Exception &error) {
        std::cerr << "ONNX Runtime error: " << error.what() << "\n";
    } catch (const std::exception &error) {
        std::cerr << "CamLayer model probe failed: " << error.what() << "\n";
    }
    return 1;
}
