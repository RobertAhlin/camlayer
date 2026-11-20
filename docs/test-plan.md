# CamLayer Test Plan

**Next target:** v0.2.8 frame-cadence verification and motion-quality regression tests.  
**Purpose:** Verify the new mask detail modes against the v0.2.7 measurements, with emphasis on preserved fine detail and signed edge behavior.

## Test setup

1. Check that Vercel published v0.2.8 from `main`, then hard-refresh the page.
2. Close other camera-using applications if they interfere with access.
3. Use the same physical webcam, camera angle, background, and lighting for every comparison.
4. Select `1280 × 720` and confirm the actual output size shown.
5. Allow readings to settle before recording a representative 10-second interval, not just one screenshot.
6. Record device and browser, software version, control settings, FPS range, average processing time, visible artifacts, and any console errors.

## Immediate regression test

Start with the last known slow configuration:

| Setting | Value |
| --- | --- |
| Threshold | 0.50 |
| Edge softness | −1.14 |
| Edge cleanup | 0.55 |
| Edge stability | 1.80 |
| Temporal stabilization | Enabled |
| Output | 1280 × 720 |

The earlier version showed **7.7 FPS / 144 ms** in one screenshot. This baseline is illustrative; a meaningful comparison requires matching camera and scene.

Then compare `Full`, `Balanced`, and `Fast` with all other settings constant. Also repeat the known expensive cases: softness `−0.07` / cleanup `0`, and softness `0` / cleanup `1`. Record FPS, all stage timings, and fine-edge artifacts. Repeat at 640 × 360 if useful.

## Visual scenarios

- Sit still for ten seconds. Look for contour flutter around hair, ears, headphones, and shoulders.
- Rotate the head quickly. Check movement lag and temporary mask holes.
- Move the head slowly. Check whether high Edge stability introduces trailing.
- Spread fingers with the hand near the face. Check thin finger details, separated fingers, and background leakage.
- Change Edge softness through negative, zero, and positive values to verify **opposite directional movement**.
- Repeat with Edge cleanup `0.00` and `1.00` to determine whether its effect is actually useful.
- Repeat with temporal stabilization disabled as a control.
- If practical, compare a visually busy background with a simpler background under similar lighting.

## Record template

| Version | Camera | Output | Threshold | Softness | Cleanup | Stability | FPS range | Processing ms | Observations |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| v0.2.7 | TBD | 1280 × 720 | 0.50 | −1.14 | 0.55 | 1.80 | TBD | TBD | Not yet tested |

## Pass criteria for continued work

- Camera remains active without errors or crashes.
- Negative softness clearly contracts the silhouette; positive softness expands it.
- The output remains transparent, not filled with the checkerboard preview.
- Hand, hair, and shoulder details do not regress unacceptably.
- Frame processing is improved compared with the v0.2.5 slowdown at matching settings.
- Any residual slowness is measured and documented before new visual features are added.

**v0.2.7 showed 41.4 FPS with both filters off, 17.7 FPS with softness −0.07, and 24.0 FPS with cleanup 1.00 in screenshot tests. v0.2.8 is not yet validated in the browser.**

## Verified live comparison (November 20, 2026)

The browser snapshots show the following at 1280 × 720, threshold 0.56, softness −0.17, cleanup 1.00 and stability 2.00:

| Detail | FPS shown | Total ms | Inference ms | Mask ms |
| --- | ---: | ---: | ---: | ---: |
| Full | 16.3 | 61 | 14.6 | 45.9 |
| Balanced | 42.8 | 23 | 12.5 | 10.4 |
| Fast | 50.3 | 20 | 13.7 | 5.7 |

Interpret the displayed FPS as processing throughput, not unique camera capture FPS. The existing `video.currentTime` gate can allow repeated processing of the same captured frame. Verify actual camera frame delivery with `requestVideoFrameCallback` or another explicit new-frame signal.

### Further checks

- Use the same person, camera, lighting, and settings while moving fingers and turning the head.
- Compare visible hair and ear artifacts across Fast, Balanced and Full.
- Record unique captured frames, dropped frames and processed updates over an interval where possible.
- Do not claim 50 FPS capture from the present screenshots.

## Later OBS test questions

- Does OBS Browser Source allow access to the webcam on the target installation?
- Is transparency preserved in the source and compositor?
- Can OBS and CamLayer share a webcam without driver conflicts?
- Does quality/performance remain usable while OBS is recording or streaming?
- If not, what alternative capture path is compatible with device policies?
