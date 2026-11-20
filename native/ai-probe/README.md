# CamLayer native AI inference validation

This is a **Windows ONNX Runtime smoke test**, not an OBS plugin and not yet live camera background removal.

## Purpose

CamLayer already has a native GPU alpha-rendering test inside OBS. Before replacing the diagnostic ellipse with a real AI mask, we must confirm that a compatible person segmentation model runs with native Windows C++ code.

The standalone command-line probe (now with a real-image mode):
1. Loads a pinned ONNX version of the MediaPipe Selfie Segmentation model.
2. Uses the official ONNX Runtime Windows x64 CPU package.
3. Validates the input tensor type and dimensions.
4. Executes one inference using an artificial constant-value image.
5. Validates that the output tensor consists of finite float32 values.
6. Reports model tensor names, shapes, output minimum/maximum/average and ONNX Runtime version.
7. Processes a real, resized portrait as planar RGB floats in [0,1] and saves a grayscale `portrait-mask.png` artifact.
8. Rejects real-image masks that are effectively constant rather than indicating foreground and background.

No camera access or user data are used. The original synthetic input is **not a visual-quality evaluation**. The added real-image fixture permits a first visual inspection only, not a reliable benchmark.

Reference portrait: [safdar-hussain1/carmine](https://github.com/safdar-hussain1/carmine/blob/main/web/public/demo/model.jpg), credited by its project to photographer **Andrea Piacquadio / Pexels** under the Pexels License. Downloaded only for the CI test and not distributed in the CamLayer repository.

The exported mask is 256 × 256 pixels with white representing foreground confidence and black background confidence. Review `portrait.jpg` alongside `portrait-mask.png` in the run's Artifacts; this confirms broad segmentation behavior but not hair quality, edge precision, or real-time latency.

## GitHub Actions

Workflow: [Verify CamLayer Native AI Model (Windows)](../../.github/workflows/verify-native-ai.yml).

Open **Actions**, select the workflow and examine the latest run. The workflow is also triggered automatically when the probe or workflow changes.

A green result now means the model can be loaded and run on the hosted Windows machine, and the reference portrait produces a nonconstant mask. Inspect the PNG to confirm that its light regions actually follow the person. It **does not** mean background removal is active in OBS. A red result requires checking the failing step; no installation is required locally.

## Model source and provenance

- Candidate: `onnx-community/mediapipe_selfie_segmentation`.
- Pinned revision: `d1eb4b9905988be58be2f6a3782e606ac9a3ca2f`.
- File: `onnx/model.onnx`.
- Expected SHA256: `3241ac4ad8aa35bdaf33946776db29f7c283a413aa0b0dacb9483594b4531aad`.
- Model card lists Apache-2.0. Validate original upstream attribution and redistribution terms before packaging a public release.
- Runtime: official Microsoft.ML.OnnxRuntime NuGet package version `1.24.2`, containing Windows x64 native binaries and C++ headers. Its redistribution/license conditions must also be included in any package.
- The original workflow incorrectly targeted a non-existent GitHub release ZIP for this version and failed with HTTP 404. The download source has been corrected to the official NuGet package; a new successful build still needs confirmation.

The workflow downloads the model from Hugging Face, ONNX Runtime from NuGet, and the public portrait from GitHub on the build runner. **The Windows workstation does not need to download or install development tools.** This smoke test does not publish a distributable ONNX runtime or OBS package.

## What comes next

After a confirmed passing inference test:

1. Review the reference-image alpha mask, confirm channel order and output activation, and measure segmentation quality against the web prototype.
2. Implement a native inference wrapper and model lifecycle, independent of OBS rendering.
3. Capture an OBS source frame safely at reduced mask resolution without blocking the graphics thread for inference.
4. Feed the resulting mask into the existing GPU alpha shader rather than replacing the working graphics filter.
5. Make the four CamLayer UI controls functional and benchmark effective FPS and latency.
6. Package all required runtime binaries, model files, licenses and installation instructions.

**Current code:** only a standalone model probe. Do not install this probe into OBS.
