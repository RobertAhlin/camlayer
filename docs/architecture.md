# Current Architecture

**As implemented in v0.4.0.** This describes the working browser prototype, not a completed OBS plugin.

## Data flow

```text
Camera (getUserMedia)
    |
HTMLVideoElement + requestVideoFrameCallback
    |
One processing pass per distinct presented frame
    |
MediaPipe ImageSegmenter (VIDEO mode, CPU delegate)
    |
Foreground confidence mask
    |
Optional bilinear resampling to postprocessing detail resolution
    |
Adaptive per-pixel temporal smoothing
    |
Optional spatial cleanup (separable box filtering)
    |
Confidence threshold -> alpha mask
    |
Signed morphology: erosion (negative) / dilation (positive)
    |
Feathering of alpha mask (Canvas 2D blur)
    |
source-in composition with webcam frame
    |
Transparent output canvas + performance indicators
```

Mask work resolution is independently selectable: Full (native mask width), Balanced (capped at 640px) or Fast (capped at 480px). The final canvas retains the actual camera/output dimensions; the smaller mask is upscaled by canvas drawing and is then composited with the full camera image.

The app selects the second confidence mask for multi-channel models or the first when the model returns a single confidence mask.

## Implementation files

- `index.html`: application entry point and resource links.
- `src/main.js`: UI, stored settings, webcam lifecycle, MediaPipe loading, composition, frame callback scheduling, and per-stage timing.
- `src/frame-timing.mjs`: metadata-based presentation counting, rolling FPS, skipped-frame tracking and stability test statistics.
- `tests/frame-timing.test.mjs`: automated diagnostics and timed-test behavior checks.
- `src/mask-processor.mjs`: reusable CPU mask processor with separate work resolution, temporal smoothing, cleanup, morphology, and RGBA output.
- `tests/mask-processor.test.mjs`: automated behavior tests for the processor.
- `src/styles.css`: responsive application styling.
- `vercel.json`: static hosting configuration.
- `README.md`: project overview.
- `docs/development-status.md`: progress and verified/unverified work.
- `docs/test-plan.md`: reproducible future checks.

## Dependencies

- MediaPipe Tasks Vision `0.10.21` loaded dynamically from a CDN.
- MediaPipe WASM assets loaded from the corresponding version.
- Selfie Segmenter Landscape model loaded remotely and executed locally after loading.
- No build pipeline, server-side image processing, database, or authentication.

A network connection may be required to load app assets and the AI dependencies. This does not imply a video upload.

## Controls and processing semantics

| Control | Range | Behavior |
| --- | --- | --- |
| Mask threshold | 0.15–0.85 | Determines the confidence level where opacity transitions |
| Edge softness | −2.00 to +2.00 | Negative shrinks foreground alpha; positive expands it. Both apply blur proportional to absolute value |
| Edge cleanup | 0.00–1.00 | Spatial smoothing of the confidence mask; visually subtle in current tests |
| Edge stability | 0.00–2.00 | Adjusts the strength of adaptive temporal filtering |
| Stabilize mask | On/off | Disables or enables temporal smoothing |
| Checkerboard | On/off | Changes only the preview background |
| Resolution | 640 × 360 / 1280 × 720 | Requests capture resolution; actual dimensions are determined by the camera/browser |

Settings are stored in `localStorage` under `camlayer.settings.v2`. Existing saved values from earlier v0.2 releases are reused.

## Current performance considerations

The model is configured with CPU inference, despite logs that may mention graphics initialization. The measured `Inference + render` timer covers `segmentForVideo` plus mask processing and canvas composition; it is not an end-to-end camera latency measurement.

Version v0.2.7 instruments the pipeline with `performance.now()` around MediaPipe inference, confidence-mask extraction and postprocessing, and canvas rendering. Results are smoothed and displayed at approximately 350 ms intervals. These timers measure synchronous main-thread wall time, not GPU activity or CPU-core/thread utilization.

Version v0.2.5 added min/max morphology with radius-dependent nested loops and caused a major observed slowdown in one 720p test. Version v0.2.6 replaces these loops with separable sliding-window extrema and reuses the queue buffer, aiming for runtime proportional to mask pixel count rather than radius times pixel count.

This change needs real-device validation. Other steps, including cleanup filtering, mask-to-image writing, and canvas blur, still have computational cost.

## OBS integration boundary

The output is currently a browser canvas. A successful transparent preview in a browser **does not prove** OBS Browser Source can access the webcam and preserve alpha, nor that OBS and the app can open the same camera simultaneously. These must be independently tested before integration claims or architecture commitments.

## Frame scheduling and stability testing

In v0.2.9 a video-frame callback is registered on the camera's `HTMLVideoElement`. When the browser supplies a new presented frame, CamLayer checks the monotonically increasing `presentedFrames` counter and processes the image only once per new counter value.

The callback is re-registered after processing. If processing takes too long, the next callback may report a gap in `presentedFrames`, which is recorded as skipped presented frames. This is a browser-compositor metric, not a measurement of camera sensor frames dropped before presentation.

A limited fallback checks `getVideoPlaybackQuality().totalVideoFrames` where available, otherwise uses a throttled animation frame loop. The final fallback is inherently approximate and is labeled as such.

The rolling three-second Presented FPS and Processed FPS counters are independent of the smoothed inference/mask/canvas processing timings. A 2/5/10 minute test accumulates average and peak processing duration, skipped presented frames, and callback gaps over the full run. No media frames are retained by the test. Results remain local to the open tab and are qualified if the page is hidden.

## OBS Browser Source experiment (v0.3.0)

The existing application now accepts `?mode=obs`: CSS hides the main UI, original video preview, controls, checkerboard and diagnostic panels while retaining the processed transparent alpha canvas. The Browser Source URL may specify `resolution=1280|640` and `detail=fast|balanced|full`. Debug mode (`debug=1`) shows status messages and manual controls for OBS Interact.

An independent `obs-test.html` page exposes translucent and opaque shapes against a truly transparent CSS background. This page does not request camera permission and should be tested first to isolate transparency behavior.

The OBS embedded browser has its own permission model and device context, which may prevent `getUserMedia`. A visual alpha test alone cannot establish webcam compatibility. This architecture does not yet implement an external-browser bridge, inter-process frame transfer or native plugin.

## Local OBS Bridge architecture (experimental v0.4.0)

```text
Chrome localhost camera + MediaPipe segmentation
     |
Transparent Canvas 2D output
     |
WebP alpha image encoding (drop frames if busy)
     |
WebSocket sender -> 127.0.0.1:8765 relay
     |
WebSocket receiver -> OBS Browser Source
     |
WebP decode -> transparent canvas over OBS scene
```

A Node.js `ws` server broadcasts encoded images to connected local receivers. A random session token is printed when the server starts; the server listens only on IPv4 loopback and checks the WebSocket Origin header. The relay does not store media or forward frames to the internet. Camera/model resources may still be downloaded over the network by the Chrome browser.

The frame-transfer protocol is a prototype, not yet tested in OBS. Image serialization can add CPU overhead and latency. A future implementation may use a faster local frame channel or OBS-native source if performance is inadequate.
