# CamLayer

**AI Background Removal for OBS Studio**

CamLayer is an experimental project for real-time AI-powered webcam background removal without a green screen. The long-term goal is to produce a clean, stable, transparent video layer that can be used with OBS Studio.

Microsoft Teams is our visual quality benchmark. Achieving comparable quality is a goal to test, not a guaranteed property of the initial model.

> **Project status:** v0.1 was validated with a real webcam. v0.2 edge-refinement and preferences update committed; live verification is pending.

## Project goals

- Segment the person in a live webcam feed.
- Preserve transparency through an alpha channel for flexible compositing.
- Prioritize natural edges, temporal stability, low latency, and practical performance.
- Process camera frames locally on the user's device, without uploading video to a server.
- Validate the standalone browser experience before deciding how to integrate with OBS Studio.

## Version 0.1: initial working MVP

The first version will be a browser application featuring:

- Start and stop controls for camera access, with explicit user permission.
- Camera enumeration and selection.
- Side-by-side previews of the original feed and the segmented output.
- A transparent output canvas and an optional checkerboard preview.
- Adjustable mask threshold and edge softness.
- Basic temporal smoothing to reduce mask flicker.
- Selectable processing resolutions, initially 640 × 360 and 1280 × 720.
- Processing FPS and approximate per-frame processing time.
- Clear error handling for unavailable cameras or denied permissions.

**Definition of done:** A user can open the browser app, enable a camera, move in front of it, and see a person-only output with a transparent background in real time.

OBS integration is **out of scope for v0.1**.

## Proposed technology stack

| Component | Technology |
| --- | --- |
| Application | Native JavaScript modules, HTML, and CSS |
| Initial segmentation engine | MediaPipe Tasks Vision |
| Compositing | Canvas 2D or WebGL |
| Source control | GitHub |
| Optional HTTPS hosting | Vercel |
| Long-term target | OBS Studio |

The segmentation layer should remain modular so alternative models can be evaluated without rewriting camera handling or the UI.

## Proposed processing pipeline

1. Request camera access after a user action using `getUserMedia`.
2. Obtain frames from the selected camera.
3. Run a person-segmentation model to generate a mask.
4. Refine the mask for smoother edges and improved temporal consistency.
5. Composite the video and mask onto a canvas with transparency.
6. Display previews and performance metrics.

All video frame processing is intended to run on the client. The browser may still download application assets and model files over the network.

## Target development environment

- Windows 11, 64-bit
- Intel Core i5-1335U
- Intel Iris Xe Graphics
- 16 GB RAM
- Sandberg external webcam, up to 1920 × 1080 at 30 FPS
- OBS Studio 33.0.1, 64-bit

The initial solution should not require administrator privileges or NVIDIA-specific acceleration. Device policies and browser permissions must be respected.

## Proposed repository structure

```text
camlayer/
├── README.md
├── vercel.json
├── index.html
├── src/
│   ├── main.js
│   └── styles.css
└── docs/
    ├── architecture.md
    └── testplan.md
```

The first implementation lives in `src/main.js` and `src/styles.css`. Future iterations may split the JavaScript into independent modules.

## Version 0.2: edge and preference improvements

- Updated initial defaults: threshold 0.35, softness 0.28.
- Edge cleanup control (0.00 to 1.00), applying a small weighted spatial filter only to uncertain mask pixels.
- Motion-aware temporal smoothing, reducing trailing artifacts when confidence changes rapidly.
- Persistent camera, resolution, threshold, softness, cleanup, checkerboard, and stabilization preferences in browser localStorage.
- No backend or build process; this is still a static JavaScript app.

**Important:** Edge cleanup is a lightweight blur of uncertain mask regions, not a new AI model. Fine details may still be lost at high threshold values. The quality and performance impact need testing with hands, hair, and movement.

## Roadmap

| Version | Scope |
| --- | --- |
| **v0.1** | Camera capture, person segmentation, transparent preview, basic adjustments, and performance monitoring |
| **v0.2** | Improved adaptive edge processing and locally persisted settings; model comparisons remain future work |
| **v0.3** | Test and select a practical OBS integration approach |
| **v1.0** | Stable workflows, verified image quality and performance, and user documentation |

## Testing and acceptance criteria

We will compare the result against Teams using the same camera, lighting conditions, and framing. Important scenarios include:

- Hair and shoulders against light and dark backgrounds.
- Fast head and hand movements.
- Hands in front of the face and objects held close to the body.
- Low-light conditions.
- Mask holes, flickering, halos, and motion trails.

We will measure FPS, latency, and CPU/GPU load. The v0.1 acceptance target is a usable live preview. Teams-like quality will require separate comparative evaluation.

## OBS integration: open questions

A transparent canvas in Chrome or Edge does **not** prove that OBS Browser Source can access the camera or preserve the alpha channel under identical conditions. Camera sharing between the app and OBS may also depend on the device driver.

We will test these constraints explicitly before selecting an integration method. Alternatives may include a separate rendering process or a native OBS plugin, subject to feasibility and device policy.

## Getting started

### Run locally

No Node.js, package installation, transpilation, or bundling is required. Serve the repository directory over localhost using any simple static HTTP server, then open the local URL in a modern browser. Camera access requires localhost or HTTPS.

### Vercel deployment

The `vercel.json` file explicitly selects the **Other** framework preset and serves files from the repository root. There is no install or build step. Connect the repository to Vercel and deploy branch `main`.

The browser downloads MediaPipe runtime code, WebAssembly assets, and the segmentation model from external hosts. Video frames are processed locally and are not sent to those hosts.

**Verification status:** The v0.1 camera pipeline ran successfully in a browser at 1280 × 720. v0.2 requires fresh deployment and quality testing.

## Privacy, security, and licensing

- Webcam video must not be uploaded to a backend by default.
- No automatic recording or cloud video analysis is planned for the MVP.
- Library and model-weight licenses must be reviewed before distribution.
- Enterprise device policies, browser permissions, and installation restrictions must be respected.

## Language convention

All repository content, including source code, code comments, documentation, commit messages, issue descriptions, and pull request descriptions, should be written in **English**.

---

**Project started:** November 4, 2026  
**Current milestone:** CamLayer v0.2 edge refinement and stored preferences
