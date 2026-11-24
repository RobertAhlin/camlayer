# Native CamLayer OBS Filter

**Status:** The original pass-through bootstrap was successfully compiled on Windows via GitHub Actions. A new GPU alpha diagnostic has now been added, but its updated build and OBS behavior are not yet verified. AI person segmentation is not yet integrated.

This folder is the starting point for a native OBS Studio plugin intended to work as a filter on a normal **Video Capture Device** source, without a second camera connection or a Chrome window.

## Files

- `src/plugin-main.c`: registers a GPU video effect filter with an optional diagnostic alpha ellipse. With the diagnostic disabled, the source is passed through visually unchanged.
- `data/locale/en-US.ini`: filter labels and notice.
- `data/ellipse_alpha.effect`: shader drawing an alpha-masked ellipse to confirm actual OBS transparency.
- `CMakeLists.txt`: initial libobs-based module target.

## Intended OBS user workflow

1. Add a Video Capture Device as usual.
2. Open its **Filters** window.
3. Add **CamLayer AI Background Removal** under **Effect Filters** or the appropriate asynchronous video filter category.
4. Configure threshold, softness, cleanup and stability.

**This is the eventual AI workflow, not a working AI feature yet.** The present version has an **optional ellipse diagnostic alpha effect** but does not identify a person.

## Important engineering constraints

- OBS accepts multiple camera pixel formats. Before image segmentation, convert or access frames safely and preserve correct timing.
- Native OBS filters need to handle source lifecycle, resolution changes and threading correctly.
- Model selection is open: the browser MediaPipe Tasks Vision JavaScript/WASM runtime cannot simply be loaded in a C module.
- A native-compatible runtime must be selected, with model format compatibility, licenses, CPU/GPU delegation and distribution reviewed.
- Person segmentation output must be composited with premultiplication/straight-alpha handling appropriate for OBS.
- Keep mask work at Balanced-like resolution initially to preserve the performance gains observed in the browser prototype.
- The original JS implementation remains a reference for effect controls and visual quality, not direct native source code.

## GitHub Actions: Windows build for beginners

The repository has a workflow named **Build CamLayer OBS plugin (Windows)** under `.github/workflows/build-native-windows.yml`. It uses the official OBS plugin template to supply its Windows build environment.

1. Open https://github.com/RobertAhlin/camlayer/actions
2. Select **Build CamLayer OBS plugin (Windows)** from the workflows list.
3. Select **Run workflow**, choose the `main` branch, then select **Run workflow**.
4. Open the new run. Green means the steps succeeded; red means we must inspect the failed step and fix the source or build configuration.
5. After a successful run, open **Artifacts** and download `camlayer-obs-windows-x64-bootstrap`.

The workflow also starts automatically after changes to `native/obs-plugin/` or to the workflow file itself.

**Do not install the artifact yet.** The first output is a build-test of a pass-through filter, not a functioning AI background-removal plugin. Check with IT before attempting to load unsigned or custom DLL plugins on a managed PC.

A GitHub Actions workflow may require enabling Actions in repository settings if disabled. It uses GitHub-hosted Windows build machines; nothing must be installed locally to trigger a build.

## Build and deployment

The first pass-through Windows build was successful in GitHub Actions run #2. The GPU alpha diagnostic is a later code change: verify its new build and artifact independently before installing. Build success alone does not confirm that OBS can load the filter or render correct transparency. `find_package(libobs CONFIG REQUIRED)` needs an actual libobs CMake package path.

The target user's Windows computer is IT-managed and may prohibit installation, even for a compiled ZIP plugin. Obtain approval before copying plugins to protected OBS directories. GitHub Actions can build the code remotely, but cannot override local security policies.

## Next technical milestones

1. Confirm the latest GPU diagnostic Windows CI build and bundled effect file.
2. On an authorized test machine, confirm filter registration and that toggling the ellipse produces real transparency without Chroma Key.
3. Choose and integrate a native segmentation runtime.
4. Convert capture frames and apply the alpha mask without blocking OBS rendering.
5. Port and benchmark mask settings, quality presets, and stabilization.
6. Package, document, and test with multiple camera formats and OBS versions.

## Signed softness parity and follow speed 0–5 (experimental)

The browser `src/mask-processor.mjs` uses a signed morphological operation **after thresholding**, not only a shader smoothstep-width adjustment. Native CamLayer now uses the same overall approach on the inference worker: threshold the confidence mask with smoothstep and apply a separable min (negative softness contracts the foreground) or max (positive softness expands the foreground) filter at 256px mask resolution. Mask movement blends continuously from zero with a radius scaled to the model image size. The GPU shader now samples the finished alpha directly, without a second cutoff that would undo the effect. Edge softness may change outline geometry and should be used with care around hair and hands.

**Edge Follow Speed now accepts 0–5:** the requested readback cadence is approximately 10 Hz at 0, 20 Hz at 1, 30 Hz at 2 and 60 Hz at 5, and motion gating accepts the full 0–5 range. These are only sampling targets. On a 30 FPS webcam or a CPU that cannot process every frame, pushing beyond 2 may increase work without increasing unique frames or reducing end-to-end delay. Preserve the working OBS version until the new Windows smoke test is green and test progressively. RVM remains a planned separate backend.

The Windows smoke tests now check that positive softness expands a synthetic silhouette, negative softness shrinks it, and follow speed 5 is accepted by the temporal processing path. These tests cannot prove real-camera softness quality or latency.

## Edge follow speed and capture responsiveness (experimental)

User testing showed that **Edge Stability around 1.50 reduces flicker**, but moving the head leaves a similarly delayed silhouette at all stability settings. The MediaPipe prototype previously sampled the camera at only ~10 frames/second, a separate source of latency from temporal smoothing.

The native filter now exposes **Edge follow speed (0–2)**, independent of Edge Stability. Higher settings reduce the historical weight specifically where mask pixels change and request more frequent 256x256 camera samples:

| Edge follow speed | Target camera-sample rate |
| --- | --- |
| 0.00 | 10 frames/s |
| 1.00 (default) | 20 frames/s |
| 2.00 | 30 frames/s |

Actual AI-mask output can be slower because GPU readback, CPU inference and OBS scheduling also take time. Increasing capture frequency may increase CPU/GPU cost. This setting does not automatically predict motion or compensate for the age of a completed mask. A bounded worker queue keeps the most recent pending image while continuing to publish successful completed masks under load rather than starving the renderer. The staged image timestamp now reflects capture time, not the later readback time.

**Suggested difficult-lighting baseline:** threshold 0.50, softness 0.44, cleanup 0.50, stability 1.50, follow speed 1.00. Test speed 1.50 and 2.00 while turning the head; if CPU load or flicker increases, step back. Verify Windows CI and OBS on-device before claiming any latency improvement. RVM remains separately planned, not implemented.

## Working cleanup and stability controls (first refinement pass)

The native pipeline now implements **Edge Cleanup** and **Edge Stability** in the inference worker rather than leaving them as placeholder sliders. These controls operate on each filter instance's own mask state.

- **Edge Cleanup, 0–1:** Reduces isolated, locally inconsistent foreground alpha at 256x256 resolution. Zero leaves the mask unchanged. This may remove small speckles and reduce some rough edges but cannot reliably separate a chair physically joined to the person's silhouette.
- **Edge Stability, 0–2:** Uses motion-adaptive temporal smoothing. Zero has no smoothing, and higher settings blend more of the preceding mask when pixels change a little. Large changes favor the new frame to reduce trailing silhouettes.
- The worker resets temporal state on camera loss or resolution change, and when its backend is restarted. Mask history is not shared between cameras.
- The build includes regression tests for zero-effect controls, isolated pixel cleanup, smooth low-motion changes, and fast motion recovery.

**Visual behavior is not yet validated in OBS.** Test first under the challenging low-light train conditions and compare with both sliders at zero. Overaggressive cleanup can remove hair detail; high stability can still produce ghosting. The refinement code is built as shared backend infrastructure so the future RVM implementation can use the same controls, though RVM has not been integrated.

## Experimental OBS live-mask path (first integration build)

The native filter now exposes **Enable live MediaPipe background removal (experimental)**, disabled by default. When enabled and the MediaPipe backend is selected, the filter creates a source-local inference worker, captures a downscaled 256x256 BGRA view of its actual OBS filter target on the graphics thread, stages pixels for CPU inference, and uploads successful 256x256 alpha masks back to a GPU texture. The shader multiplies the source alpha by a mask whose cutoff and softness use the corresponding filter sliders.

The initial readback is deliberately throttled to approximately 10 captures per second, while rendering remains controlled by OBS. The downscaled GPU staging buffer is mapped after a later capture interval to avoid immediately reading the same image just written. This is a **first functional prototype**, not a quality/performance validated implementation: GPU readback, image orientation, edge alignment, frame latency and camera changes require on-device tests. The mask is displayed only after the first successful inference. Before that, output remains unmasked. The original ellipse test remains available and overrides the live mask when enabled.

MediaPipe is the only implemented live backend. RVM remains selectable but unavailable, with a shared worker interface ready for a separate recurrent-model implementation. The edge cleanup and edge stability sliders now have an initial CPU refinement implementation; visual validation is pending. Changing camera or resolution resets the visible mask to avoid using old masks with mismatched dimensions. Because model loading, CPU inference, GPU staging and source rendering can fail independently, test with a noncritical OBS scene, leave live mode disabled if problems occur, and use the OBS log entry `[CamLayer] First live MediaPipe alpha mask uploaded to GPU` to establish that a real model result reached the shader.

## Shared asynchronous inference worker (development milestone)

`src/ai-worker.h` and `src/ai-worker.cpp` provide a bounded, latest-frame-only background worker that can serve different AI backends. A distinct worker instance belongs to each camera/filter instance in the final integration.

- **MediaPipe:** a background thread initializes the ONNX session, consumes copied 256x256 BGRA frames, and produces a timestamped 256x256 float alpha mask
- **RVM:** the same public interface identifies the engine, but reports `UNAVAILABLE` until recurrent-state inference is implemented; no false ready status
- Caller-side submit only copies/downsamples pixels; model inference is never performed on the caller's thread
- New pending frames overwrite old pending frames. An obsolete inference result is discarded when a newer frame is already queued
- A worker is shut down and joined before deletion. Camera disconnection, engine switching and model shutdown must use this lifecycle
- The Windows smoke test exercises model initialization, frame submission, result retrieval, timestamp integrity and the unavailable RVM placeholder

**Not yet connected to live OBS camera frames.** The filter still uses its existing optional model loader and verified GPU ellipse. The worker is compiled into the Windows plugin artifact and validated by a separate smoke test. Live camera capture, worker lifecycle integration in the OBS filter, safe mask texture upload and alpha rendering remain next steps. This staged approach avoids replacing the currently working on-device path before it is end-to-end validated.

## Native ONNX runtime integration (experimental)

The plugin now has a C ABI bridge in `src/ai-engine.h` and a C++ implementation in `src/ai-engine.cpp`. The bridge can:

- Load the pinned MediaPipe Selfie Segmentation ONNX file through ONNX Runtime.
- Convert a 640x360, 1280x720 or other BGRA image to NCHW RGB 256x256.
- Run inference and return a 256x256 floating-point alpha mask with values in [0,1].
- Release model resources explicitly.

The **Load native MediaPipe AI runtime** checkbox opt-in initializes the model from the plugin's bundled `data/models/` directory and logs success or a detailed failure in OBS's log file. The model is not yet fed live OBS frames. RVM has only a selection placeholder.

The Windows GitHub Actions build now downloads the same pinned model and ONNX Runtime binaries previously verified by the standalone native AI workflow, compiles the C++ implementation into `camlayer-obs.dll`, and places the `onnxruntime.dll` beside the OBS plugin. It includes a separate smoke-test executable exercising the same engine API with a synthetic BGRA frame, to catch integration regressions.

**This is experimental infrastructure, not live AI background removal yet.** Preserve the ellipse test as the only verified alpha-rendering path until the camera-to-model-to-GPU pipeline has been tested. Enabling the loader during OBS use may cause a brief initialization pause, and must be done only in an authorized test environment.

Next implementation: safely sample the actual OBS camera image, send one bounded frame at a time to a background inference worker (not the OBS graphics render thread), upload the resulting alpha mask to a GPU texture, and replace the ellipse only after successful end-to-end tests. Handle model lifecycle, camera size changes, backend switching and concurrent frames carefully.

## Dual AI-engine selection scaffold

The native OBS filter now exposes an **AI engine (planned)** dropdown with:

- **MediaPipe (Fast, integration pending)**
- **Robust Video Matting (Quality, integration pending)**

OBS saves the selected engine with the filter settings. Switching the option currently does **not** run either model; in both cases the last verified GPU pass-through/diagnostic ellipse remains intact. This deliberate staging lets us test the dropdown without accidentally claiming working background removal.

### Planned backend interface

Each implementation should provide initialization, model shutdown, per-frame inference, status reporting, and a shared grayscale alpha mask. MediaPipe will process independent frames, while RVM will maintain recurrent per-stream state and reset it when the input changes, video seeks, or a new engine is selected.

The GPU renderer and mask-refinement controls can then consume whichever alpha mask is active. Avoid running neural inference synchronously inside OBS's graphics rendering callback; use a bounded frame queue and well-defined frame ownership.

Before shipping either backend, verify the separate model-weight redistribution conditions in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md), test CPU and GPU performance, and include all native runtime dependencies in the artifact.

## First alpha diagnostic test

Once the updated artifact builds successfully, and only where installing a custom OBS plugin is authorized:

1. Install the package's `camlayer-obs` folder in OBS's recognized per-user plugin location, retaining its `bin/64bit` and `data` folder layout.
2. Restart OBS, add a normal Video Capture Device and place a color/image source behind it.
3. Add the CamLayer filter from the source's Effect Filters list.
4. Enable **Diagnostic transparency test (ellipse)**. The outer area should become transparent, revealing the background behind the camera.
5. Disable the toggle. The full camera should reappear.

The ellipse intentionally does not follow the person. Mask threshold, softness, cleanup and stability sliders are present for future AI integration but are not operational yet in the native plugin.

**Do not test by copying source C files or CMake files into OBS.** Use only a verified built artifact, and follow the device organization's IT rules.
