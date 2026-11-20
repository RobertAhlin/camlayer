# CamLayer Development Status

**Date:** November 4, 2026  
**Repository:** `RobertAhlin/camlayer`  
**Deployed URL:** https://camlayer.vercel.app/  
**Code version:** v0.2.7  
**Last known implementation commit before this documentation update:** `5b52c83b28c99fea3acc3cf1a0f6dd6d554b09db`  
**Verification:** v0.2.6 was subsequently reported at approximately 15 FPS. v0.2.7 diagnostics have been committed but not tested in the user's browser.

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

## Current stop point

CamLayer v0.2.7 is in `main`. The user reported around 15 FPS after v0.2.6. The v0.2.7 diagnostic display has not yet been validated after deployment.

### First actions next session

1. Confirm v0.2.7 deployed through Vercel and reload the page.
2. Select the same webcam, `1280 × 720` if available, and identical visual settings to the prior test.
3. Record **AI inference**, **Mask processing**, **Canvas rendering**, **Total processing**, and **Processing FPS** after the readings settle.
4. Compare the results at Edge softness `0.00` and `−1.14` while holding every other setting fixed.
5. Repeat at `640 × 360` to understand how costs scale with output resolution.
6. Use the measured bottleneck to choose the next change. Investigate MediaPipe GPU delegate if inference dominates; optimize mask arrays / morphology if mask processing dominates; investigate alpha blur or canvas composition if canvas rendering dominates.
7. Test visual quality and responsiveness before claiming any performance gain.

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
