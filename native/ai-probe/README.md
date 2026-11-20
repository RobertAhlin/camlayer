# CamLayer native AI inference validation

This is a **Windows ONNX Runtime smoke test**, not an OBS plugin and not yet live camera background removal.

## Purpose

CamLayer already has a native GPU alpha-rendering test inside OBS. Before replacing the diagnostic ellipse with a real AI mask, we must confirm that a compatible person segmentation model runs with native Windows C++ code.

The standalone command-line probe:
1. Loads a pinned ONNX version of the MediaPipe Selfie Segmentation model.
2. Uses the official ONNX Runtime Windows x64 CPU package.
3. Validates the input tensor type and dimensions.
4. Executes one inference using an artificial constant-value image.
5. Validates that the output tensor consists of finite float32 values.
6. Reports model tensor names, shapes, output minimum/maximum/average and ONNX Runtime version.

No camera access, video frames, or user data are used. The synthetic input is **not a visual-quality evaluation**.

## GitHub Actions

Workflow: [Verify CamLayer Native AI Model (Windows)](../../.github/workflows/verify-native-ai.yml).

Open **Actions**, select the workflow and examine the latest run. The workflow is also triggered automatically when the probe or workflow changes.

A green result means the model can be loaded and run on the hosted Windows machine. It **does not** mean background removal is active in OBS. A red result requires checking the failing step; no installation is required locally.

## Model source and provenance

- Candidate: `onnx-community/mediapipe_selfie_segmentation`.
- Pinned revision: `d1eb4b9905988be58be2f6a3782e606ac9a3ca2f`.
- File: `onnx/model.onnx`.
- Expected SHA256: `3241ac4ad8aa35bdaf33946776db29f7c283a413aa0b0dacb9483594b4531aad`.
- Model card lists Apache-2.0. Validate original upstream attribution and redistribution terms before packaging a public release.
- Runtime: official Microsoft ONNX Runtime CPU x64 version `1.24.2`. Its redistribution/license conditions must also be included in any package.

The workflow downloads the model from Hugging Face and the runtime from GitHub on the build runner. **The Windows workstation does not need to download or install development tools.** This smoke test does not publish a distributable ONNX runtime or OBS package.

## What comes next

After a confirmed passing inference test:

1. Establish the model's exact preprocessing and output activation semantics using a reference image and visual comparison.
2. Implement a native inference wrapper and model lifecycle, independent of OBS rendering.
3. Capture an OBS source frame safely at reduced mask resolution without blocking the graphics thread for inference.
4. Feed the resulting mask into the existing GPU alpha shader rather than replacing the working graphics filter.
5. Make the four CamLayer UI controls functional and benchmark effective FPS and latency.
6. Package all required runtime binaries, model files, licenses and installation instructions.

**Current code:** only a standalone model probe. Do not install this probe into OBS.
