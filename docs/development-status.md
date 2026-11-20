# CamLayer Development Status

**Last updated:** November 20, 2026  
**Repository:** `RobertAhlin/camlayer`  
**Deployed URL:** https://camlayer.vercel.app/  
**Browser version:** v0.4.2  
**Native plugin status:** initial C/CMake scaffold, not yet compiled  
**Latest feature commit before this documentation update:** `a4220943d51bbd5e911f02bedcd69f27d0839ad6`  
**Verification:** v0.2.7 measurements confirmed mask postprocessing as the primary bottleneck. The v0.2.8 detail modes are now verified to run in the browser, with a significant processing-speed improvement. Fine-detail motion tests remain open.

## Product objective

Build a real-time, local-first AI webcam background removal tool whose output has transparent alpha and can eventually be used in OBS Studio. Microsoft Teams is the quality comparison target. The initial delivery is a browser demo; OBS integration remains a separate, unverified milestone.

## Day-one decisions

1. Use the project name **CamLayer** and GitHub repository `camlayer`.
2. Keep code, UI strings, commits, and repository documentation in English.
3. Initially attempted TypeScript + Vite, but simplified to static HTML, CSS, and JavaScript to remove the build step and allow Vercel to deploy directly.
4. Use MediaPipe Tasks Vision and the Selfie Segmenter Landscape model rather than training a model from scratch.
5. Process video locally in the browser. Hosting distributes code and model dependencies but does not process camera frames.
6. Preserve the working browser MVP before deciding on an OBS integration method.

## Implementation timeline

| Release | Main change | Observed result / qualification |
| --- | --- | --- |
| v0.1 | Camera selection, person segmentation, transparent output, threshold, basic softness, FPS | First end-to-end working camera demo; encouraging initial visual quality |
| v0.2 | Refined mask smoothing, Edge cleanup, saved preferences | Edge cleanup was hard for the user to see visually |
| v0.2.1 | Edge softness extended to 2.00 with actual blur | User observed a more noticeable softness effect |
| v0.2.2 | Motion-adaptive Edge stability control | User reported some improvement in contour flutter |
| v0.2.3 | Edge stability extended to 2.00 | Value was initially not updating on screen; subsequently fixed |
| v0.2.4 | Signed softness with confidence cutoff offset | Both signs looked too similar; geometry was not being changed strongly enough |
| v0.2.5 | Actual inward erosion / outward dilation of alpha mask | Directional change implemented, but processing performance dropped sharply |
| v0.2.6 | Sliding-window min/max morphology optimization | User later reported about 15 FPS; still below early versions |
| v0.2.7 | Per-stage inference / mask / canvas timing | Implemented; **not yet verified** on the target webcam |

## Troubleshooting lessons

- The first Vercel builds failed after adding a build pipeline. Moving to a zero-build static site allowed successful publication.
- The first MediaPipe import endpoint returned HTTP 404. The application was switched to version `0.10.21`.
- The selfie model returned a single foreground confidence mask; code initially assumed a mask at index 1 and was changed to also handle index 0.
- The Edge stability slider initially did not update its displayed number; the input event handler was corrected.
- Signed Edge softness based only on confidence threshold did not produce the desired visibly inward/outward adjustment. Alpha-mask morphology replaced it.
- The first morphological implementation used nested radius loops and was computationally expensive at 720p. v0.2.6 changes this to separable monotonic sliding windows.

## Observed visual findings

- The first working result preserved a fairly convincing face, glasses, headphones, shoulders, and most of the hand.
- At a low threshold around `0.17`, more thin fingers were retained but there were more unwanted remnants / halos.
- At a high threshold around `0.85`, finger details could break apart or disappear.
- Complex background content and confidence fluctuation may contribute to visible edge flutter. This causal explanation is plausible but not formally isolated.
- The user reported Edge stability made a noticeable difference, especially at stronger settings.
- At high positive Edge softness there was visible outer halo; the user requested negative values to move the effect inward.
- No systematic objective measurement of segmentation quality or temporal jitter has yet been made.

## Performance snapshots from user screenshots

| Configuration observed | FPS | Inference + render | Output |
| --- | ---: | ---: | --- |
| Initial alpha preview | 35.1 | 27 ms | 1280 × 720 |
| Hand test | 36.4 | 23 ms | 1280 × 720 |
| Later edge refinement | 25.1 | 38 ms | 1280 × 720 |
| v0.2.5, threshold 0.50, softness −1.14, cleanup 0.55, stability 1.80 | 7.7 | 144 ms | 1280 × 720 |

Different camera selections and software revisions were used across screenshots. These are illustrative single-frame readings, not controlled like-for-like results. The 7.7 FPS reading is the latest reported measurement; it is from v0.2.5, **before** optimization.

## Follow-up measurement

After v0.2.6 the user reported approximately **15 processing FPS**. That is an improvement over the v0.2.5 7.7 FPS observation, but below the earlier prototype performance. Resolution and exact settings were not reconfirmed in that message. No per-stage timing data was available yet.

### v0.2.7 diagnostics

The live performance panel now shows smoothed wall-clock time for:
- AI inference (MediaPipe `segmentForVideo`).
- Mask processing (confidence-mask extraction plus postprocessing).
- Canvas rendering (mask feathering and video compositing).
- Total processing, including minor result cleanup and timing overhead.

These measurements do **not** count CPU threads or measure end-to-end camera latency. No CPU/GPU delegate change was made in this release.

## v0.2.7 measured bottleneck

The user compared three settings in 1280 × 720 with threshold 0.85 and Edge stability 1.75:

| Softness | Cleanup | FPS | Mask processing |
| ---: | ---: | ---: | ---: |
| 0.00 | 0.00 | 41.4 | 10.9 ms |
| −0.07 | 0.00 | 17.7 | 37.2 ms |
| 0.00 | 1.00 | 24.0 | 26.6 ms |

Even a very small nonzero edge offset invokes a costly full-size morphology pass. Cleanup also adds significant time. Inference remained approximately 12.6–18.1 ms, while canvas rendering was below 1 ms in these screenshots.

## v0.2.8 implementation

- Extracts mask processing to a reusable, independently tested module.
- Adds three postprocessing detail levels while leaving camera output resolution unchanged.
- Reuses processing buffers, avoiding per-frame scratch array allocation for cleanup and morphology.
- Uses bilinear sampling in reduced detail modes.
- Keeps the previous visual controls and sets Balanced as the default detail mode.
- Fixes the cramped diagnostic panel width.
- CPU/GPU delegate remains unchanged to isolate the quality/performance tradeoff.

Synthetic Node benchmarks and logic tests are not a substitute for browser validation. Visual quality, FPS, and possible detail loss around hair and fingers remain to be assessed.

## November 20: real-browser v0.2.8 comparison

The user tested the same webcam and output size of 1280 × 720 at threshold 0.56, Edge softness −0.17, cleanup 1.00, stability 2.00, stabilization enabled:

| Detail | Reported processing FPS | Total time | AI inference | Mask processing | Canvas |
| --- | ---: | ---: | ---: | ---: | ---: |
| Balanced | 42.8 | 23 ms | 12.5 ms | 10.4 ms | 0.3 ms |
| Full | 16.3 | 61 ms | 14.6 ms | 45.9 ms | 0.4 ms |
| Fast | 50.3 | 20 ms | 13.7 ms | 5.7 ms | 0.3 ms |

Balanced reduced mask processing from 45.9 to 10.4 ms compared with Full in these screenshots. Fast reduced it to 5.7 ms. All three screenshots showed usable static person segmentation; some background remains near one side of the head. Dynamic edge fidelity is **not yet verified**.

**Frame-rate caveat:** The loop uses `requestAnimationFrame` plus `video.currentTime`, not an explicit new-camera-frame callback. `currentTime` can advance between unique captured video frames, so 50.3 processing FPS is not evidence the webcam supplied 50.3 distinct frames per second. Fix this measurement and avoid redundant inference next.

## v0.2.9: camera frame cadence and repeatable stability testing

- Replaced the old `requestAnimationFrame` + `video.currentTime` gate with `requestVideoFrameCallback` on supporting browsers.
- Reads `presentedFrames` metadata to prevent repeated processing of the same presented frame and to count frames skipped between callbacks.
- Uses a cautious fallback when video-frame callbacks are unavailable; non-metadata fallback FPS is labeled as estimated.
- Separates Presented FPS from Processed FPS and keeps detailed per-stage timings.
- Adds a 2, 5 or 10 minute stability test with elapsed time, average processed FPS, mean/worst processing time, missed presented frames and long processing gaps.
- Flags tests where the document becomes hidden, because background tab throttling can distort results.
- Keeps `Balanced` as the default mask detail, without changing MediaPipe or the mask processing algorithm.

**Verification outstanding:** Confirm v0.2.9 startup, callback behavior, camera FPS labels and at least one complete stability test in the browser. A lower processed FPS than the previous 50.3 figure is expected if that figure included repeated video frames.

## v0.3.0: first OBS Browser Source integration experiment

- Added `obs-test.html`, a camera-free alpha compatibility test.
- Added `?mode=obs` to the existing application; hides the normal UI and checkerboard while rendering only the transparent output canvas.
- Added optional source URL settings for capture resolution and mask detail.
- Added `?mode=obs&debug=1` for visible error status and camera controls through OBS Interact.
- The OBS mode attempts camera startup, but permission support in OBS embedded Chromium has **not** been verified.
- Kept the existing browser application, selected model and Balanced default intact.

**Next test:** In OBS create a Browser Source for `/obs-test.html`, confirm that its background is truly transparent, then test the `?mode=obs` source. If camera permissions fail, investigate a local browser-to-OBS transfer path before committing to a native plugin.

## v0.4.0: Local OBS Bridge prototype

The first OBS source trial confirmed that alpha compositing works, but OBS embedded browser showed `Permission denied` for camera access. Chrome was able to capture and segment the camera independently.

The experimental next step is a local relay:

- `bridge/server.mjs` starts a loopback HTTP and WebSocket server on port 8765.
- `/?mode=bridge&token=...` runs the normal CamLayer UI in Chrome and sends transparent WebP frames.
- `/bridge/output.html?token=...` runs in OBS Browser Source and renders the received frame with alpha.
- A random per-start token limits unauthorized local connection attempts; the relay also checks the request origin.
- The OBS source never invokes `getUserMedia`, so it does not require webcam permission.
- The relay favors the newest image over buffering indefinitely, and clears on stop or disconnection.

**Not yet validated:** Windows installation, Webcam-to-OBS frame transport, WebP alpha quality, encoding overhead, and overall end-to-end latency. The browser prototype remains the known-good fallback. This bridge is NOT implemented on Vercel.

### Next actions

1. Clone/pull latest repository to the Windows machine.
2. Install Node.js 20+ if necessary, run `cd bridge`, `npm install`, `npm start`.
3. Open the generated Chrome sender URL, start the camera, then add the generated OBS Browser Source URL (1280 × 720).
4. Verify transparency over a visible OBS background and check movement, image quality, lag, and CPU load.
5. Record any connection, decoding, or permission errors; do not claim completed OBS integration until tested.

## Current stop point

CamLayer v0.2.8 is in `main` after committing this documentation. The user demonstrated the v0.2.7 performance breakdown; v0.2.8 still needs a real-camera comparison.

### Next actions

1. Confirm Vercel displays the v0.3.0 release.
2. Create an OBS Browser Source from `https://camlayer.vercel.app/obs-test.html` and confirm transparency over a colored background.
3. Create a second OBS Browser Source from `https://camlayer.vercel.app/?mode=obs&resolution=1280&detail=balanced`, 1280 × 720.
4. If the camera source is blank, test the `&debug=1` URL via OBS Interact and capture the exact permission/error message.
5. If camera access succeeds, verify the alpha channel, moving hands, and usable FPS under OBS load. If it fails, compare alternative local transfer architectures.
6. Do not characterize the browser-source experiment as a working OBS plugin until it has been validated.

## Planned investigations after stabilization

- Determine whether Edge cleanup provides enough value to justify remaining user-visible.
- Consider separating edge position from feather radius if signed softness couples too many visual changes.
- Compare model alternatives only if measured quality shortcomings justify a switch.
- Add repeatable diagnostics for model inference versus postprocessing costs.
- Explore OBS Browser Source permissions, alpha preservation, and potential webcam conflicts. A native plugin is an alternative, not a predetermined solution.

## Environment and constraints

- Windows 11, 64-bit.
- Intel Core i5-1335U, Intel Iris Xe Graphics, 16 GB RAM.
- Sandberg external camera shown as GENERAL WEBCAM; integrated HP camera is also available.
- OBS Studio 33.0.1, 64-bit.
- Traditional executable installation may be restricted by device policies; the browser MVP avoids an installer.
- GitHub repository and connected Vercel project.

## v0.4.1: Windows managed-device workaround

The user cannot install Node.js because their computer is IT-managed. Added `?mode=chroma`, which keeps the camera in Chrome and displays the alpha-composited person against a flat green backdrop. The OBS Window Capture source can then apply its built-in Chroma Key filter. This avoids installing any runtime, but necessarily re-keys the image instead of preserving the original alpha directly. The native camera pipeline and existing OBS modes are unchanged. **Manual testing pending.**

## v0.4.2: Chroma Output mask controls

After verifying the Chroma Output video can be captured by OBS, the user found that the hidden refinement controls prevented tuning out leftover scene fragments. Added an **Adjust mask** button to the Chrome Chroma Output page. It opens a temporary on-screen settings drawer with the original four sliders and stabilization setting. Close it with the new button or Escape before OBS capture. All preferences remain browser-local. Manual testing is pending.

## Native OBS plugin direction (November 20, 2026)

Following the successful browser prototype and limited OBS Chroma Output integration, the project focus is now a **native OBS video filter** attached directly to a Video Capture Device source.

Created `native/obs-plugin/` with an initial CMake build definition, C filter registration, and English locale strings. The first stage is explicitly a **pass-through filter**, not an AI background remover. No Windows binary has been built or tested. The next milestone is to establish a pinned Windows CI build and verify filter loading before porting segmentation.

The target workstation does not allow installation of Node.js and may restrict OBS plugin installation. GitHub-hosted builds are proposed, but installing a resulting plugin must still comply with IT policy.

## Native OBS GPU alpha diagnostic (November 20, 2026)

After the initial native Windows pass-through build produced a DLL and locale files in GitHub Actions run #2, the next implementation milestone is the alpha rendering path.

- Native filter switched from asynchronous CPU video manipulation to OBS's supported graphics filter pathway.
- Added `data/ellipse_alpha.effect` as an optional ellipse-shaped alpha diagnostic, rendered with a GPU shader.
- Extended UI with `Diagnostic transparency test (ellipse)` toggle.
- Updated the Windows GitHub Actions workflow to bundle the shader with the compiled module.
- Kept Mask threshold, Edge softness, Edge cleanup and Edge stability controls as **placeholders** until native model inference exists.
- The diagnostic renders a geometric oval, **not a person mask**.

**Validation required:** Wait for the current Windows GitHub Actions run, inspect the built artifact for `bin/64bit/camlayer-obs.dll` and `data/ellipse_alpha.effect`, then test in OBS on a machine where plugin installation is permitted. If the shader fails to load, review the OBS log rather than attributing it to AI inference.

**Next AI design decision:** Select a native-compatible model execution runtime and validate that the browser model or an equivalent segmentation model can be distributed and run offline with suitable CPU/GPU performance. The JavaScript MediaPipe runtime cannot simply be linked into the native C plugin.

## Native inference smoke test (November 20, 2026)

Implemented `native/ai-probe/src/model-probe.cpp`, a C++ ONNX Runtime smoke test that attempts to load a pinned MediaPipe Selfie Segmentation ONNX model, runs one synthetic image inference, validates float output, and reports tensor metadata.

The GitHub Actions workflow `verify-native-ai.yml` downloads a pinned ONNX Runtime CPU Windows x64 release and a model at fixed revision, verifies the model SHA256, compiles the native C++ probe, runs inference, and publishes the probe executable only if successful.

**Status:** Sources and CI were committed. The inference run itself has **not yet been independently verified** by reviewing a successful workflow log. It is not integrated into the OBS plugin, so the live plugin continues to display the GPU ellipse diagnostic, not AI person segmentation.

After green CI: confirm the model's actual input layout, scaling and output activation before implementing asynchronous capture, inference and alpha upload in OBS. Keep a fallback to the existing verified ellipse diagnostic, and avoid tying real-time inference to OBS's graphics render thread.

## Real-image native segmentation verification (November 20, 2026)

GitHub Actions run #8 confirmed that ONNX Runtime 1.24.2 executed the ONNX model with a synthetic tensor and returned an output of shape 1x1x256x256. The constant synthetic input gave an all-zero output, which does not establish useful person segmentation.

New probe code now accepts a P6 RGB portrait (256x256), packs channels as normalized NCHW float32 in [0,1], exports the 256x256 mask as PGM, and checks that the output is nonconstant. The workflow fetches a publicly available Pexels-licensed portrait from a third-party sample project, generates the input PPM and converts the mask into PNG. It uploads the source photo and mask for manual comparison, without uploading either into the repository.

**The first real-image workflow result and visual mask quality are not yet verified in this record.** It must pass CI and be inspected before live inference is added to the OBS filter. The existing GPU ellipse diagnostic remains the verified on-device fallback.
