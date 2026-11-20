# CamLayer

**AI Background Removal for OBS Studio**

CamLayer is an experimental, browser-based real-time webcam background removal application. It uses on-device person segmentation to create a video canvas with a transparent background, with eventual integration into OBS Studio as the long-term goal.

**Visual quality reference:** Microsoft Teams. Comparable quality remains a target to evaluate, not a verified claim.

**Live demo:** https://camlayer.vercel.app/  
**Current code version:** v0.2.8  
**Development status:** Working browser prototype. Live v0.2.7 measurements identified mask postprocessing as the main performance bottleneck. v0.2.8 adds selectable mask-processing detail levels and buffer reuse; real-browser screenshots now confirm a large reduction in mask processing time, with the effect on moving fine details still to be evaluated.

## Current capabilities

- Camera selection and browser permission handling, with start and stop controls.
- Side-by-side camera and transparent-alpha previews.
- MediaPipe Tasks Vision person segmentation processed in the browser.
- Camera capture profiles of 640 × 360 and 1280 × 720, plus an independent mask postprocessing detail selector (Fast 480px, Balanced 640px, Full native mask resolution).
- Adjustable mask threshold (0.15–0.85).
- Signed Edge softness (−2.00 to +2.00), which erodes the alpha mask inward for negative values or dilates it outward for positive values, then feathers the result.
- Edge cleanup (0.00–1.00) for spatial confidence-mask smoothing.
- Edge stability (0.00–2.00) for motion-adaptive temporal smoothing, with an on/off stabilization toggle.
- Optional checkerboard transparency preview.
- Processing FPS, smoothed total processing time, separate AI inference / mask processing / canvas rendering timing, and output dimensions.
- Browser-local saved preferences through `localStorage`.

The effect of Edge cleanup has been difficult to distinguish in informal visual tests; its utility remains open for evaluation.

## What has been verified

A browser-hosted camera pipeline has worked in live user testing, with working person segmentation and transparent output. Hair, glasses, headphones, shoulders, and fingers were explored at various mask thresholds. Edge stability visibly reduced some contour flicker in user testing, and signed softness provided an inward/outward contour adjustment.

Observed measurements are snapshots, **not benchmarks**:

| Test state | Processing FPS | Inference + render | Output |
| --- | ---: | ---: | --- |
| Early v0.1 test | 35.1 | 27 ms | 1280 × 720 |
| Later v0.1 test | 36.4 | 23 ms | 1280 × 720 |
| v0.2 edge-adjustment test | 25.1 | 38 ms | 1280 × 720 |
| v0.2.5 morphological processing test | 7.7 | 144 ms | 1280 × 720 |
| v0.2.6 user follow-up (approximate) | ~15 | Not reported | Not reconfirmed |

**Update:** v0.2.6 was subsequently reported at approximately 15 FPS, improving over v0.2.5 but still below early versions. v0.2.7 adds diagnostics to identify which stage is consuming time. Its timings are not yet verified on the target machine.

See [Development Status](docs/development-status.md) for the exact stopping point and [Test Plan](docs/test-plan.md) for the next checks.

## Technology and deployment

| Component | Current choice |
| --- | --- |
| Application | Static HTML, CSS, and native JavaScript modules |
| AI engine | MediaPipe Tasks Vision, version 0.10.21 |
| Segmentation model | MediaPipe Selfie Segmenter Landscape |
| Composition | Canvas 2D and alpha mask |
| Source control | GitHub, `main` |
| Hosting | Vercel static site |
| Backend | None |

All video frames are intended to remain on the client. The application downloads its JavaScript AI runtime, WASM assets, and model from external endpoints on first use. No video upload or recording is implemented.

### Run and deploy

No npm install, TypeScript compiler, or bundler is required. Serve the repository root from a local HTTP server on `localhost`, or use the connected Vercel project. Webcam access needs a secure context such as HTTPS or localhost.

The repository's `vercel.json` configures a static website without an installation or build step. Vercel publishes the `main` branch.

## Repository layout

```text
camlayer/
├── README.md
├── index.html
├── vercel.json
├── .gitignore
├── src/
│   ├── main.js
│   ├── mask-processor.mjs
│   └── styles.css
├── tests/
│   └── mask-processor.test.mjs
└── docs/
    ├── development-status.md
    ├── architecture.md
    └── test-plan.md
```

Current processing and UI logic are in `src/main.js`. Further modularization is a future improvement, not a prerequisite for testing.

## v0.2.8 live measurements (November 20, 2026)

At 1280 × 720 output, with threshold 0.56, Edge softness −0.17, Edge cleanup 1.00, Edge stability 2.00, stabilization enabled, the same external webcam, and the same room:

| Mask detail | Processing FPS (reported) | Total processing | AI inference | Mask processing | Canvas rendering |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full | 16.3 | 61 ms | 14.6 ms | 45.9 ms | 0.4 ms |
| Balanced | 42.8 | 23 ms | 12.5 ms | 10.4 ms | 0.3 ms |
| Fast | 50.3 | 20 ms | 13.7 ms | 5.7 ms | 0.3 ms |

These are screenshots of live, smoothed readings, not formal benchmarks. At this fixed scene and settings, Balanced improved reported processing throughput by roughly 2.6× versus Full, while Fast reduced mask processing time by roughly 8.1× versus Full.

**Caution:** The current rendering loop uses `requestAnimationFrame` and checks `video.currentTime`, which advances during playback even without a new captured frame. Consequently, the displayed processing FPS can exceed the camera's actual unique-frame rate, and some frames may be reprocessed. A future iteration should use `HTMLVideoElement.requestVideoFrameCallback` where supported, with a fallback for older browsers, and label processed frame throughput separately from captured unique frames.

Still to test: fast hand movement, fine hair and fingers, background leakage near the ears/shoulders, and prolonged use. The screenshots suggest Fast and Balanced look similar for this mostly stationary scene but are not sufficient for a full quality conclusion.

## v0.2.8 mask processing optimization

- Extracts reusable pure JavaScript postprocessing into `src/mask-processor.mjs`.
- Adds a `Mask detail` selector: **Balanced (640 px)**, **Fast (480 px)** and **Full (native model mask resolution)**. This controls postprocessing resolution, **not the capture/output video resolution**.
- Resamples confidence masks bilinearly before spatial processing in Fast and Balanced modes.
- Reuses the confidence, morphology, filtering and RGBA buffers across frames; skips optional filtering when controls are zero.
- Preserves motion-adaptive stabilization, signed inward/outward edge morphology, and alpha compositing.
- Fixes the cramped timing breakdown layout.
- Includes automated Node tests for mask directionality, image sizes, zero-filter fidelity, and buffer reuse.
- Synthetic processor-only measurements showed lower postprocessing cost in Fast/Balanced modes; the **end-to-end result in the real browser has not yet been verified**.

### Why this version was necessary

The following v0.2.7 observations were provided at 1280 × 720 with threshold 0.85 and Edge stability 1.75:

| Edge softness | Edge cleanup | Processing FPS | Mask processing |
| ---: | ---: | ---: | ---: |
| 0.00 | 0.00 | 41.4 | 10.9 ms |
| −0.07 | 0.00 | 17.7 | 37.2 ms |
| 0.00 | 1.00 | 24.0 | 26.6 ms |

This isolates mask morphology and cleanup as major costs while AI inference remains relatively fast. Different scene frames and normal runtime variability still affect comparisons.

## v0.2.7 diagnostics

- Adds separate, smoothed millisecond readings for MediaPipe inference, mask extraction and processing, and canvas composition.
- Keeps existing model, CPU delegate, mask algorithm, and visual controls unchanged so timing comparisons remain meaningful.
- Updates readings approximately every 350 ms to reduce UI-update overhead.
- Measurements are CPU wall-clock durations on the main thread, not total camera latency, GPU execution time, or CPU thread utilization.

## Roadmap

- **v0.1:** First working browser camera and segmentation preview (completed).
- **v0.2.x:** Refinement controls, preferences, edge stability, signed morphology, and performance tuning (in progress).
- **Next:** Verify v0.2.6 correctness and performance, then consider simplification and further quality improvements.
- **v0.3 (planned):** Test practical ways to provide an alpha-enabled source to OBS Studio.
- **v1.0 (future):** Stable integration, tested quality and latency, installation and user documentation.

## Known limitations

- Strong inward edge adjustments can remove thin details such as hair or fingers. Strong outward settings may reveal halos.
- The mask can flicker with complex backgrounds and movement; strong stabilization can introduce trailing, especially for slow motion.
- Processing FPS is not the same as camera FPS; inference and render time is not end-to-end latency.
- OBS Browser Source camera permissions, alpha handling, and camera sharing have **not** been validated.
- No formal comparison with Teams, extended stability run, or model license review has been completed.

## Documentation

- [Development Status](docs/development-status.md): day-one history, tested behavior, current blockers, and precise continuation steps.
- [Architecture](docs/architecture.md): current capture, segmentation, mask-processing, and rendering pipeline.
- [Test Plan](docs/test-plan.md): repeatable quality and performance checks for the next development session.

## Repository language

All source code, code comments, user-facing app strings, documentation, commit messages, issues, and pull request descriptions are written in **English**. Project discussions may use other languages.

---

**Project started:** November 4, 2026  
**Last documented status:** November 20, 2026  
**Next milestone:** Measure unique captured frames separately from processing throughput, then test moving fine details across Fast, Balanced and Full.
