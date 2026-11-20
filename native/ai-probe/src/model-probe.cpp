// CamLayer native segmentation runtime smoke test.
// This is a command-line probe, not a real-time OBS integration.
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// The source PPM must be resized to 256x256 before invoking this probe.
// P6 RGB bytes are mapped to planar NCHW floats in [0,1], matching
// the pinned model's do_rescale=true, do_normalize=false configuration.
static std::vector<float> load_ppm_rgb(const std::string &path, int width, int height)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open test image: " + path);

    std::string magic;
    int w = 0, h = 0, max_value = 0;
    file >> magic >> w >> h >> max_value;
    if (!file || magic != "P6" || w != width || h != height || max_value != 255)
        throw std::runtime_error("Expected P6 RGB PPM with 256x256 resolution and maxval 255.");
    char separator = 0;
    file.get(separator);
    if (!file || !std::isspace(static_cast<unsigned char>(separator)))
        throw std::runtime_error("Invalid PPM pixel separator.");

    const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<uint8_t> interleaved(pixels * 3);
    file.read(reinterpret_cast<char *>(interleaved.data()),
              static_cast<std::streamsize>(interleaved.size()));
    if (file.gcount() != static_cast<std::streamsize>(interleaved.size()))
        throw std::runtime_error("Incomplete PPM image.");
    std::vector<float> planar(interleaved.size());
    for (size_t i = 0; i < pixels; ++i) {
        for (size_t channel = 0; channel < 3; ++channel)
            planar[channel * pixels + i] = interleaved[3 * i + channel] / 255.0f;
    }
    return planar;
}

static void save_pgm_mask(const std::string &path, const float *mask, size_t count,
                          int width, int height)
{
    if (count != static_cast<size_t>(width) * static_cast<size_t>(height))
        throw std::runtime_error("Unexpected mask size.");
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not create mask file.");
    file << "P5\n" << width << " " << height << "\n255\n";
    for (size_t i = 0; i < count; ++i) {
        const float value = std::clamp(mask[i], 0.0f, 1.0f);
        const unsigned char sample = static_cast<unsigned char>(value * 255.0f + 0.5f);
        file.put(static_cast<char>(sample));
    }
    if (!file) throw std::runtime_error("Failed writing mask file.");
}

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
    if (argc != 2 && argc != 4) {
        std::cerr << "Usage: camlayer-model-probe <model.onnx> [portrait.ppm output-mask.pgm]\n";
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
        if (input_shape.size() != 4 || (input_shape[0] != 1 && input_shape[0] != -1))
            throw std::runtime_error("Expected a 4D image input with batch=1 or dynamic batch.");
        if (input_shape[0] == -1) input_shape[0] = 1;
        // Other dynamic dimensions require explicit preprocessing decisions.
        for (const auto dim : input_shape) {
            if (dim <= 0) throw std::runtime_error("Unsupported dynamic spatial/channel dimension.");
        }

        const size_t elements = checked_element_count(input_shape);
        const bool image_test = argc == 4;
        // Real input images use fixed 256x256 RGB layout, otherwise maintain
        // the original synthetic test to detect model/runtime regressions.
        std::vector<float> image = image_test
            ? load_ppm_rgb(argv[2], 256, 256)
            : std::vector<float>(elements, 0.5f);
        if (image.size() != elements)
            throw std::runtime_error("Image size does not match the model tensor.");
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

        std::cout << "ONNX Runtime: " << OrtGetApiBase()->GetVersionString() << "\n";
        std::cout << "Input: " << input_name.get() << " [" << describe_shape(input_shape)
                  << "] float32\n";
        std::cout << "Output: " << output_name.get() << " [" << describe_shape(output_shape)
                  << "] float32\n";
        std::cout << "Output stats: min=" << minimum << " max=" << maximum
                  << " mean=" << average << "\n";
        if (image_test) {
            if (output_shape.size() != 4 || output_shape[0] != 1 ||
                output_shape[1] != 1 || output_shape[2] != 256 || output_shape[3] != 256)
                throw std::runtime_error("Expected a 1x1x256x256 mask.");
            save_pgm_mask(argv[3], mask, output_elements, 256, 256);
            std::cout << "Saved real-image mask: " << argv[3] << "\n";
            if (maximum - minimum < 0.05f)
                throw std::runtime_error("Reference portrait produced an almost constant mask.");
            std::cout << "Real-image mask: PASS\n";
        }
        std::cout << "CamLayer native model inference: PASS\n";
        return 0;
    } catch (const Ort::Exception &error) {
        std::cerr << "ONNX Runtime error: " << error.what() << "\n";
    } catch (const std::exception &error) {
        std::cerr << "CamLayer model probe failed: " << error.what() << "\n";
    }
    return 1;
}
