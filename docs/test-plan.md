# CamLayer Test Plan

**Next target:** v0.4.0 Local OBS Bridge manual compatibility experiment.  
**Purpose:** Verify the new mask detail modes against the v0.2.7 measurements, with emphasis on preserved fine detail and signed edge behavior.

## v0.4.0 local bridge test

1. Clone or pull the repository on the Windows test computer.
2. Run `cd bridge`, then `npm install`, then `npm start` with Node.js 20+.
3. Keep the terminal open; copy the printed Chrome sender URL into Chrome.
4. Start CamLayer with the desired camera, 1280 × 720 and Balanced detail.
5. In OBS add a Browser Source using the server's printed receiver URL, width 1280, height 720.
6. Verify the person appears over a background with real transparency and that both camera movement and output stay live.
7. Stop the sender: the OBS output should clear. Start again to verify recovery.
8. Measure OBS output latency, CPU utilization, FPS, flicker and fine edges; record the findings.
9. If blank, add `&debug=1` to the receiver URL and inspect the local server logs.

**Status:** This prototype has not yet been validated on the user's Windows/OBS installation. No webcam images leave the local server.

## OBS compatibility steps

1. In OBS add a colored background source for reference.
2. Add a Browser Source using `https://camlayer.vercel.app/obs-test.html`, width 1280 and height 720. Confirm the colored background remains visible outside the floating test shapes.
3. Add another Browser Source using `https://camlayer.vercel.app/?mode=obs&resolution=1280&detail=balanced`, again 1280 × 720.
4. Confirm camera permission and alpha output if supported. Ensure the webcam is not already locked by an OBS Video Capture source.
5. If blank, change the source URL to include `&debug=1`, right-click the Browser Source and choose Interact, and record the exact error message.
6. If camera access fails, record that as a Browser Source limitation and evaluate alternative local integration approaches; do not fake alpha with a black background or claim success without a working feed.
7. Test streaming/recording overhead, transparency, and image quality only after actual OBS camera access succeeds.

**Status:** No OBS integration test has been completed in the target environment yet.

## Test setup

1. Check that Vercel published v0.2.9 from `main`, then hard-refresh the page.
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

## v0.2.9 cadence and stability test

1. Verify the `v0.2.9` label and confirm the camera frame callback is active in the live performance panel.
2. At 1280 × 720 and Balanced detail, compare Presented FPS, Processed FPS and processing milliseconds. A nominal 30 FPS webcam should not be reported as 50 unique captured frames.
3. Run a 2-minute stability test while visible, confirm the elapsed time advances and a final summary appears.
4. Run a 10-minute stability test with head movements, hands near the face and periods without movement.
5. Record average processed FPS, average/worst processing time, skipped presented frames and gaps over 250 ms. Note any flicker, slowness or unexpected camera shutdown.
6. Change browser tabs briefly in a separate test and verify the report flags the test as backgrounded.
7. Stop the camera and check that the webcam indicator turns off and callbacks cease.
8. If `requestVideoFrameCallback` is unavailable, validate fallback mode separately and do not treat its frame count as exact.

A passing manual test requires the app to remain responsive, output alpha to remain valid, camera to stop cleanly, reported FPS to reflect distinct processed frames, and visible performance not to degrade unacceptably. **No v0.2.9 pass result has yet been recorded.**

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
