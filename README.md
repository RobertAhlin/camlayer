# CamLayer

**AI Background Removal for OBS Studio**

CamLayer is an experimental, browser-based real-time webcam background removal application. It uses on-device person segmentation to create a video canvas with a transparent background, with eventual integration into OBS Studio as the long-term goal.

**Visual quality reference:** Microsoft Teams. Comparable quality remains a target to evaluate, not a verified claim.

**Live demo:** https://camlayer.vercel.app/  
**Current code version:** v0.2.6  
**Development status as of November 4, 2026:** Working browser prototype; the newest performance optimization awaits a real-camera retest.

## Current capabilities

- Camera selection and browser permission handling, with start and stop controls.
- Side-by-side camera and transparent-alpha previews.
- MediaPipe Tasks Vision person segmentation processed in the browser.
- Processing profiles of 640 × 360 and 1280 × 720.
- Adjustable mask threshold (0.15–0.85).
- Signed Edge softness (−2.00 to +2.00), which erodes the alpha mask inward for negative values or dilates it outward for positive values, then feathers the result.
- Edge cleanup (0.00–1.00) for spatial confidence-mask smoothing.
- Edge stability (0.00–2.00) for motion-adaptive temporal smoothing, with an on/off stabilization toggle.
- Optional checkerboard transparency preview.
- Processing FPS, approximate inference-and-render time, and output dimensions.
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

**Important:** v0.2.6 replaces the costly morphological filter with a linear-time sliding-window implementation. It is committed but was **not yet retested** when this status was written. Do not assume the earlier FPS has been recovered.

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
│   └── styles.css
└── docs/
    ├── development-status.md
    ├── architecture.md
    └── test-plan.md
```

Current processing and UI logic are in `src/main.js`. Further modularization is a future improvement, not a prerequisite for testing.

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
**Last documented status:** November 4, 2026  
**Next milestone:** Verify CamLayer v0.2.6 before starting OBS integration.
