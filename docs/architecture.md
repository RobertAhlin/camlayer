# Current Architecture

**As implemented in v0.2.6.** This describes the working browser prototype, not a completed OBS plugin.

## Data flow

```text
Camera (getUserMedia)
    |
HTMLVideoElement
    |
MediaPipe ImageSegmenter (VIDEO mode, CPU delegate)
    |
Foreground confidence mask
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

The app selects the second confidence mask for multi-channel models or the first when the model returns a single confidence mask.

## Implementation files

- `index.html`: application entry point and resource links.
- `src/main.js`: UI, stored settings, webcam lifecycle, MediaPipe loading, segmentation, postprocessing, and rendering.
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

Version v0.2.5 added min/max morphology with radius-dependent nested loops and caused a major observed slowdown in one 720p test. Version v0.2.6 replaces these loops with separable sliding-window extrema and reuses the queue buffer, aiming for runtime proportional to mask pixel count rather than radius times pixel count.

This change needs real-device validation. Other steps, including cleanup filtering, mask-to-image writing, and canvas blur, still have computational cost.

## OBS integration boundary

The output is currently a browser canvas. A successful transparent preview in a browser **does not prove** OBS Browser Source can access the webcam and preserve alpha, nor that OBS and the app can open the same camera simultaneously. These must be independently tested before integration claims or architecture commitments.
