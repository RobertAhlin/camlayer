# Native CamLayer OBS Plugin

**Experimental Windows x64 OBS background-removal filter**, tested with MediaPipe and RVM on live cameras. The native plugin is the main CamLayer product; the browser demo is a separate proof of concept.

## Current status (27 November 2026)

RVM MobileNetV3 FP32 ONNX is working with stateful video inference and selectable **256, 384, 512, 768 and 1024** square input/output masks. Real-device testing with a Sandberg webcam found that **512 × 512 substantially improves the contour over 256 × 256** and has tolerable lag for ordinary seated use, although Capture follow speed needs to be high. **768 and 1024 lag too far behind movement** on the tested CPU, despite looking good when stationary. These are observations, not representative FPS benchmarks.

MediaPipe remains at 256 × 256. Both engines use CPU inference through ONNX Runtime and render alpha through the OBS GPU effect. DirectML/GPU inference and live performance metrics are future work.

## Download and installation

The [Windows build workflow](https://github.com/RobertAhlin/camlayer/actions/workflows/build-native-windows.yml) uploads `camlayer-obs-windows-x64-bootstrap` on green runs, retained for 14 days. You may need a GitHub login to download an Actions artifact. The [verified November 27 test build](https://github.com/RobertAhlin/camlayer/actions/runs/45991937183) includes RVM resolutions through 1024.

1. Download and extract the ZIP from a **successful** workflow run.
2. Close OBS.
3. Copy the entire `camlayer-obs` folder into the OBS plugin directory, commonly `C:\\Program Files\\obs-studio\\obs-plugins\\` on standard Windows installs. Preserve `bin/64bit` and `data` subfolders.
4. Confirm that `bin/64bit/camlayer-obs.dll` and `onnxruntime.dll` exist, alongside `data/ellipse_alpha.effect`, `data/locale/en-US.ini` and `data/models/*.onnx`.
5. Launch OBS, add a Video Capture Device, open **Filters**, add the **CamLayer AI Background Removal** Effect Filter and enable live AI background removal. Choose RVM or MediaPipe.

The plugin does not need a separate Node.js/Python process or browser. Follow your organization's software installation policy. Always back up an existing working version before replacing plugin files.

## Recommended RVM first test

- Select **RVM**, **512 × 512** and enable live AI.
- Start with the default alpha black/white points, then tune against your camera and lighting.
- **Enable edge handling** is off by default. Enable it to show and apply Cleanup, Extra temporal stability, Capture follow speed, Gaussian Edge softness, Gaussian feather width and Feather bias. Settings are preserved while toggling the option.
- If motion appears delayed, lower temporal stability; increasing Capture follow speed affects sampling and motion history but **cannot accelerate CPU model inference**.
- Compare 256, 384 and 512 while turning your head. Use 768 and 1024 **only experimentally** with attention to mask lag.

| RVM size | Status |
| --- | --- |
| 256 | Reference; fastest |
| 384 | Intermediate |
| **512** | Best quality/performance compromise in current user test |
| 768 | Runs, but movement is too slow on tested computer |
| 1024 | Runs, but movement is too slow on tested computer |

### What the plugin does

Camera image is captured in the graphics callback at the selected mask size, copied to a one-slot asynchronous worker queue, processed with RVM recurrent ONNX inference or MediaPipe segmentation, optionally refined and uploaded as a GPU alpha mask. RVM outputs a continuous alpha matte. Its optional feather uses a Gaussian filter on the continuous matte rather than binary silhouette thresholding. A camera geometry or engine change resets appropriate state.

## CI, packaging and source

The Windows workflow builds the plugin against the official OBS plugin template and tests the native MediaPipe model, RVM recurrent inference at **256, 384, 512, 768, 1024**, and Gaussian edge behavior. This verifies compatibility with that CI environment, not the performance or OBS integration of all possible webcams.

The compiled test artifact is available through GitHub Actions. A public [GitHub Releases](https://github.com/RobertAhlin/camlayer/releases) prerelease should be published **only after reviewing the distribution rights and license notices for both bundled models and ONNX Runtime**. See [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

Source files: `src/plugin-main.c`, `src/ai-worker.cpp`, `src/ai-engine.cpp`, `src/rvm-engine.cpp`, `src/ai-mask-refine.cpp`, `src/rvm-alpha-feather.h`, `data/ellipse_alpha.effect`, `data/locale/en-US.ini`.

Current work lives on the `feature/per-engine-matting-controls` branch in [PR #1](https://github.com/RobertAhlin/camlayer/pull/1) and may not yet be in `main`.

## Next development priorities

1. Measure actual RVM inference milliseconds, delivered mask FPS and delay relative to live video.
2. Investigate GPU inference with DirectML for Windows.
3. Improve real-time follow without damaging hair/edge quality.
4. Test OBS compatibility, reliability and cross-device performance.
5. Finish the distribution/license review and release an installable preview ZIP.

**Status:** A user-tested development prototype, not production-certified software.
