# CamLayer

**AI Background Removal for OBS Studio**

CamLayer is an experimental, browser-based real-time webcam background removal application. It uses on-device person segmentation to create a video canvas with a transparent background, with eventual integration into OBS Studio as the long-term goal.

**Visual quality reference:** Microsoft Teams. Comparable quality remains a target to evaluate, not a verified claim.

**Live demo:** https://camlayer.vercel.app/  
**Browser demo version:** v0.4.2  
**Native OBS plugin:** bootstrap source code added; build and model integration pending  
**Development status:** Browser-based segmentation and the temporary OBS Chroma Output have been tested. The long-term product target is now a native OBS video filter. Its first pass-through scaffold is committed but has not yet been compiled or installed.

## Native OBS plugin roadmap (primary product direction)

The long-term deliverable is **CamLayer AI Background Removal**, a native OBS Studio video filter applied directly to a camera source. It must not require Chrome, green-screen chroma keying, a local Node.js server, or access to camera hardware from OBS's embedded browser.

A first native **pass-through bootstrap** is now available in [native/obs-plugin](native/obs-plugin/README.md), with C source, CMake and English UI strings. Its first pass-through build compiled successfully on GitHub Actions. The next native milestone now adds a GPU-based ellipse alpha diagnostic; its updated build and actual OBS transparency still require verification. **AI segmentation is not yet implemented in the plugin.**

Planned steps:

1. Confirm the updated Windows build containing the GPU alpha diagnostic.
2. Test native filter registration and true alpha transparency on a machine where plugins can be installed.
3. Select and integrate a native-compatible segmentation runtime and model.
4. Implement alpha compositing, port the edge controls and optimize latency/performance.
5. Add packaging and distribution with a clear permissions/install procedure.

The development workstation is IT-managed. Remote compilation on GitHub does **not** bypass software installation restrictions. Any deployment to this machine must follow its IT policy.

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
├── obs-test.html
├── bridge/
│   ├── server.mjs
│   ├── sender.mjs
│   ├── output.html
│   ├── output.js
│   └── package.json
├── index.html
├── vercel.json
├── .gitignore
├── src/
│   ├── main.js
│   ├── frame-timing.mjs
│   ├── mask-processor.mjs
│   └── styles.css
├── tests/
│   ├── mask-processor.test.mjs
│   └── frame-timing.test.mjs
└── docs/
    ├── development-status.md
    ├── architecture.md
    └── test-plan.md
```

Current processing and UI logic are in `src/main.js`. Further modularization is a future improvement, not a prerequisite for testing.

## v0.4.2: adjustable mask in Chroma Output

The Chroma Output view now includes an **Adjust mask** toggle beside Start and Stop. It opens the familiar Mask threshold, Edge softness, Edge cleanup, Edge stability and stabilization controls as a temporary overlay while the camera keeps running.

- Tune the controls until unwanted background remnants disappear.
- Click **Hide mask controls** or press **Escape** to close the overlay before recording or streaming in OBS.
- Settings still persist in browser localStorage and apply to the normal CamLayer view.
- Chrome Window Capture still includes any controls while visible, so always close the drawer before capturing a clean output.

## v0.4.1: No-install Chrome Window Capture fallback

If a locked-down PC does not allow Node.js or new applications, use:

```text
https://camlayer.vercel.app/?mode=chroma&resolution=1280&detail=balanced
```

1. Open the URL in Chrome (not OBS Browser Source). Click **Start camera**; Chrome can request camera access normally.
2. Press **F11** to make Chrome full-screen. This output page is a camera cutout over a solid, vivid green screen with only small overlay controls.
3. In OBS, add **Window Capture** and select the Chrome window. If Window Capture is blank, try a different Windows capture method available in OBS. Do not minimize the Chrome window.
4. Add an OBS **Chroma Key** filter to the Window Capture source, with **Green** as the key color. Adjust Similarity and Smoothness conservatively to preserve hair and fingers.
5. Crop out the tiny controls at the top if necessary, and place the resulting source above your chosen OBS background.

No Node.js, npm, new browser extension or extra OBS plugin is needed. This approach is an **interim workaround**: OBS re-keys the already-separated image, so edge quality may be slightly worse than true alpha. The output is not an alpha transport, and Chrome must remain available for capture. Do not infer that OBS Browser Source camera permissions have changed.

The existing localhost Node bridge remains an experimental option only on machines where installing/running Node is permitted.

## v0.4.0: Local OBS Bridge prototype

This is the first local-only OBS integration prototype. It uses a small Node.js relay running on `127.0.0.1:8765`, a Chrome capture page, and an OBS Browser Source display page. OBS never requests access to the webcam.

### Requirements

- Windows with Node.js 20 or newer installed.
- OBS Studio with Browser Source.
- Chrome or a Chromium browser with webcam access.
- Network access on the first load to download the MediaPipe model/runtime.

### Start on Windows

Open a PowerShell terminal in the repository:

```powershell
cd bridge
npm install
npm start
```

The terminal prints **two session-specific URLs**. The token is randomly generated every time the bridge starts.

1. Open the **Chrome sender URL** printed by the server. Select a camera, select Balanced, and click **Start camera**.
2. In OBS create a **Browser Source** at the **OBS output URL** printed by the server. Set source width to 1280 and height to 720. Keep Chrome and the terminal open.
3. The transparent composited video should appear as an overlay in OBS. If debugging, append `&debug=1` to the OBS source URL.

A Windows firewall dialog is not normally necessary because the server listens only on the computer's IPv4 loopback address. Do not bind this experimental bridge to a public network interface. Keep the randomly generated URL token private.

### How it works

The sender converts the transparent composition canvas to WebP frames, sends each image over localhost WebSocket to the relay, and OBS decodes and draws the newest frames onto a transparent canvas.

- Frame encoding, WebSocket transfer and decoding introduce latency and CPU overhead. We must test responsiveness in OBS rather than assuming performance from the normal preview.
- To avoid unbounded queues, frames are skipped when the sender is busy encoding or network buffers are full.
- The code stays local, but runtime and model assets are downloaded from existing third-party CDNs on first use. No webcam image is sent to Vercel.
- Browser Source permissions are bypassed because OBS only reads an output page, not the camera.
- This is not a native OBS plugin or a production-ready video transport. WebP alpha, CEF decoding, latency and long-run stability require hands-on OBS testing.

Do **not** use the Vercel URL with `?mode=bridge`. The relay exists only on the local Node.js server, not on Vercel.

## v0.3.0: experimental OBS Browser Source compatibility

The first OBS experiment has two independent checks:

1. **Transparency test (no webcam):** add `https://camlayer.vercel.app/obs-test.html` as an OBS Browser Source. The area outside the visible test shapes should show your OBS scene beneath.
2. **Camera permission and alpha output test:** add `https://camlayer.vercel.app/?mode=obs&resolution=1280&detail=balanced` as an OBS Browser Source, using 1280 × 720 width/height. If OBS's embedded Chromium/CEF allows access to the selected camera, CamLayer attempts to start and outputs only the transparent video canvas, with no UI or checkerboard.

If the OBS camera source does not start, test `https://camlayer.vercel.app/?mode=obs&debug=1&resolution=1280&detail=balanced`, then right-click the Browser Source and choose **Interact** to inspect error messages and use the visible start/stop controls. The debug mode is temporary and should not be used in production scenes.

**Important:** These are compatibility experiments, not a completed OBS integration. OBS Browser Source camera permissions may block `getUserMedia` even when it works in Chrome. If that happens, do not assume a webcam or MediaPipe failure; the next architecture should use a separate browser processing process and a tested local transfer method or a native OBS plugin.

The normal `https://camlayer.vercel.app/` UI remains unchanged, including Balanced as the default mask detail. OBS URLs request 1280 × 720 and Balanced by default; no separate build step or backend is introduced.

OBS Browser Source settings to start with: **Width: 1280, Height: 720**. Do not add the original webcam as a competing source while testing camera capture, because driver sharing may vary.

## v0.2.9: one processing pass per presented video frame

CamLayer now uses `HTMLVideoElement.requestVideoFrameCallback()` when available. The browser calls the processing loop when a new video frame is submitted to the compositor. Unlike `requestAnimationFrame` combined with `video.currentTime`, this avoids unnecessary reprocessing of the same presented frame.

Live diagnostics now distinguish:

- **Presented FPS:** Frame presentation rate observed using video-frame metadata. This is not a measurement of the camera sensor's physical frame rate.
- **Processed FPS:** Unique presented frames CamLayer actually processed. Normally this cannot exceed Presented FPS, except for minor short-window measurement differences.
- **Skipped presented frames:** Gaps in `presentedFrames` metadata between callbacks. These gaps indicate frames presented but not individually processed; they do not measure camera hardware drops.

Older browsers fall back to `requestAnimationFrame` and, where available, the video playback frame counter. If the counter is unavailable, the fallback is explicitly labeled as **estimated**; one-to-one processing cannot be guaranteed.

### Timed stability test

After starting the camera, use **Start test** to collect a **2, 5 or 10 minute** test session. The results show:

- Elapsed time, average processed FPS and mean/worst processing time.
- Frames presented but not individually processed.
- Long gaps between processed frames (more than 250 ms).
- Whether the tab was hidden during the test, which may invalidate the comparison.

The test runs in the open browser tab. It is not a background job or an automatic long-term monitor. Keep the tab visible and the computer awake. No camera video or recordings are saved or sent to a server.

**Important:** Correct frame scheduling can lower the displayed processing FPS compared with v0.2.8 because duplicate work no longer inflates that number. That is a measurement correction, not necessarily a performance regression.

### Quick test

1. Open the latest deployment and confirm `v0.2.9`.
2. Select a webcam, `1280 × 720`, and **Balanced** mask detail.
3. Start the camera. On a nominal 30 FPS camera, Presented FPS should approach the frame rate delivered to the compositor; Processed FPS should not exceed that for sustained intervals.
4. Start a **2 minute** test, then try **10 minutes** with normal head/hand movement and the tab visible.
5. Compare the test summary, missed frame count, processing time, and visual stability.

Automated logic tests are provided in `tests/frame-timing.test.mjs` and can be run locally with `node --test tests/frame-timing.test.mjs`. No production build step is required.

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
**Next milestone:** Validate the GPU-based diagnostic ellipse in OBS, then replace the test geometry with native model inference and a real person mask.
