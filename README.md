# CamLayer

**AI background removal for OBS Studio** | Open source, experimental

CamLayer is an experimental native OBS Studio video filter that removes a webcam background using local AI inference. Select **MediaPipe** for the lighter segmentation option or **Robust Video Matting (RVM)** for higher-quality temporal matting. The browser proof of concept is also available at [camlayer.vercel.app](https://camlayer.vercel.app/), but the **native OBS plugin is the primary product**.

> **Status on 27 November 2026:** Native MediaPipe and RVM have both been tested live in OBS on Windows. RVM at **512 × 512** provides noticeably better cutout quality than 256 × 256 in informal user testing with a Sandberg webcam, with some tolerable mask lag during ordinary seated use. 768 and 1024 produce usable stationary masks but lag unacceptably when moving on the tested CPU. This is a development preview, **not a production-ready or broadly benchmarked release**.

## Download a Windows test build

**Windows x64 only.** The latest successful [Build CamLayer OBS plugin (Windows)](https://github.com/RobertAhlin/camlayer/actions/workflows/build-native-windows.yml) workflow provides a ZIP in the run's **Artifacts** section named `camlayer-obs-windows-x64-bootstrap`. GitHub may require you to sign in to download Actions artifacts, and artifacts are kept for **14 days**.

- [Most recent successful 768/1024 Windows build, 27 November 2026](https://github.com/RobertAhlin/camlayer/actions/runs/45991937183)
- [Windows build workflow](https://github.com/RobertAhlin/camlayer/actions/workflows/build-native-windows.yml)
- [GitHub Releases](https://github.com/RobertAhlin/camlayer/releases) for future downloadable, versioned preview packages

**Public releases are not published yet.** The Actions artifact is the currently available compiled test build. A manual prerelease publishing path is being prepared, subject to a review of model-weight redistribution terms and bundled third-party notices. Do not mistake a green build for an endorsement of production use.

### Install a test build

1. Download and extract the Windows x64 artifact from a **green** Actions run.
2. Close OBS Studio. Locate the packaged `camlayer-obs` folder, which contains `bin/64bit/camlayer-obs.dll`, `bin/64bit/onnxruntime.dll`, and `data/` with the shader, locale and both ONNX model files.
3. Copy the complete `camlayer-obs` plugin folder into your OBS plugins directory (for a standard Windows installation, commonly `C:\\Program Files\\obs-studio\\obs-plugins\\`; use the appropriate plugin directory for your setup). Keep the folder structure intact.
4. Start OBS. Add a **Video Capture Device** and open **Filters**. Under **Effect Filters**, add **CamLayer AI Background Removal**.
5. Choose an AI engine and select **Enable live AI background removal (experimental)**. Start with **RVM 256 × 256** or **512 × 512** for a quality comparison. Disable diagnostic ellipse mode.

The plugin loads its ONNX models locally and does not require Python, Node.js or a browser when running in OBS. Custom plugins may be restricted by corporate device policies. Install only on a machine where you have permission. Installation details and development notes: [native/obs-plugin/README.md](native/obs-plugin/README.md).

## Native plugin capabilities

| Capability | Current status |
| --- | --- |
| OBS video-effect filter | Implemented and tested on OBS 33.0.1 on Windows |
| MediaPipe native AI segmentation | Implemented, CPU ONNX Runtime, 256 × 256 mask |
| RVM MobileNetV3 FP32 | Implemented with recurrent state across frames |
| RVM mask resolution | **256, 384, 512, 768, 1024**, at native ONNX input/output size |
| RVM alpha black / white points | Visible by default, always active |
| Optional RVM edge handling | **Enable edge handling** reveals optional cleanup, stability, follow speed, Gaussian softness, width and bias |
| Independent settings per engine | Implemented |
| GPU alpha compositing | Implemented in native OBS effect |
| Worker thread / bounded latest-pending-frame queue | Implemented |
| GPU acceleration of ONNX inference | **Not implemented**; current inference is CPU based |
| In-plugin inference FPS and latency statistics | **Not implemented** |
| macOS / Linux packages | Not available |

RVM resolution selection changes the actual camera capture stage, model input, alpha output and mask texture dimensions. Switching the resolution recreates model state and related graphics resources. MediaPipe remains fixed at 256 × 256.

### Real-camera observations, 27 November 2026

These are **qualitative observations from one Windows computer and webcams**, not controlled performance benchmarks.

| RVM resolution | User-reported observation |
| --- | --- |
| 256 × 256 | Working baseline, visibly less detail than 512 |
| 384 × 384 | Available; no specific comparative performance claim yet |
| **512 × 512** | Clear visual improvement over 256, modest movement lag but acceptable for typical seated use |
| 768 × 768 | Good still result, unacceptable mask follow lag during movement |
| 1024 × 1024 | Good still result, unacceptable mask follow lag during movement |

On the tested setup, **RVM Capture follow speed** needs to be near its maximum value (10) for the preferred 512 mode. This setting affects sampling attempts and temporal motion handling, **not** ONNX inference throughput. Capture requests are capped at 60 Hz and the model may produce masks much more slowly. High temporal stability can introduce additional lag.

For now, **512 × 512 is the practical quality reference**. Next work should measure actual inference time and delivered mask FPS, then evaluate Windows DirectML/GPU acceleration before pursuing even larger masks.

## Building and testing

Source is under [native/obs-plugin](native/obs-plugin). The [Windows Actions workflow](.github/workflows/build-native-windows.yml) uses the OBS plugin template, pinned ONNX Runtime 1.24.2, and SHA256-verified MediaPipe and RVM ONNX models. A successful build runs tests for native MediaPipe inference, RVM recurrence and reset, Gaussian feathering, and actual RVM inference at **256, 384, 512, 768 and 1024**. These tests do not establish on-device frame rates or visual quality.

The current experimental work is maintained on [feature/per-engine-matting-controls](https://github.com/RobertAhlin/camlayer/tree/feature/per-engine-matting-controls) and reviewed in [pull request #1](https://github.com/RobertAhlin/camlayer/pull/1); it is **not necessarily merged into `main`**. Select that branch for a matching workflow run or source checkout.

## Browser proof of concept

The separate [browser demo](https://camlayer.vercel.app/) uses MediaPipe Tasks Vision, transparent canvas output and adjustable edge processing. See [docs/development-status.md](docs/development-status.md) for historical browser development, and [docs/test-plan.md](docs/test-plan.md) for testing guidance. Browser version information is not the native plugin's version.

## Known limitations and next priorities

- CPU-only ONNX inference can lag the video frame; 768 and 1024 were too slow for ordinary movement on the tested hardware.
- Mask edges, hair, glasses, chairs, color spill and stability depend on lighting, webcam and settings.
- Gaussian feather improves transitions but cannot reconstruct detail that the model did not segment.
- Multiple cameras, a range of OBS versions, installer/updater behavior and long-term stability need broader testing.
- No built-in realtime RVM inference FPS / latency display and no GPU inference backend yet.
- Packaging for public redistribution needs complete third-party license and model-weight review; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

**Planned next steps:** add live inference timing and actual mask FPS diagnostics; investigate DirectML GPU acceleration on Windows; assess high-resolution performance; prepare an explicitly labeled GitHub prerelease after redistribution review.

## License and third parties

CamLayer source code is licensed [GPL-3.0-only](LICENSE). Third-party assets, ONNX Runtime binaries, pretrained model weights and OBS components retain their own licensing terms. **The project license alone is not evidence that every model weight can be redistributed.** Review [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before creating a public binary release.

Source: [GitHub](https://github.com/RobertAhlin/camlayer) | [Browser demo](https://camlayer.vercel.app/) | [Windows builds](https://github.com/RobertAhlin/camlayer/actions/workflows/build-native-windows.yml)
