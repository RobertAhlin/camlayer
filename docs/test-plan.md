# CamLayer Test Plan

**Next target:** v0.2.6 on the Windows / Intel Iris Xe development machine.  
**Purpose:** Confirm performance recovery and preserve directional edge quality after optimization.

## Test setup

1. Check that Vercel published v0.2.6 from `main`, then hard-refresh the page.
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

Then test softness `0.00` and `+1.14` while holding all other values constant.

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
| v0.2.6 | TBD | 1280 × 720 | 0.50 | −1.14 | 0.55 | 1.80 | TBD | TBD | Not yet tested |

## Pass criteria for continued work

- Camera remains active without errors or crashes.
- Negative softness clearly contracts the silhouette; positive softness expands it.
- The output remains transparent, not filled with the checkerboard preview.
- Hand, hair, and shoulder details do not regress unacceptably.
- Frame processing is improved compared with the v0.2.5 slowdown at matching settings.
- Any residual slowness is measured and documented before new visual features are added.

**No pass or fail result has been recorded for v0.2.6 yet.**

## Later OBS test questions

- Does OBS Browser Source allow access to the webcam on the target installation?
- Is transparency preserved in the source and compositor?
- Can OBS and CamLayer share a webcam without driver conflicts?
- Does quality/performance remain usable while OBS is recording or streaming?
- If not, what alternative capture path is compatible with device policies?
