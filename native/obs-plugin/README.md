# Native CamLayer OBS Filter

**Status:** CMake/C source bootstrap only. Not yet compiled or installed. Not yet a working person segmentation filter.

This folder is the starting point for a native OBS Studio plugin intended to work as a filter on a normal **Video Capture Device** source, without a second camera connection or a Chrome window.

## Files

- `src/plugin-main.c`: registers an asynchronous video filter and currently returns the original frame unchanged.
- `data/locale/en-US.ini`: filter labels and notice.
- `CMakeLists.txt`: initial libobs-based module target.

## Intended OBS user workflow

1. Add a Video Capture Device as usual.
2. Open its **Filters** window.
3. Add **CamLayer AI Background Removal** under **Effect Filters** or the appropriate asynchronous video filter category.
4. Configure threshold, softness, cleanup and stability.

**This is the target workflow, not a working feature yet.** The current bootstrap does not load MediaPipe or produce an alpha mask.

## Important engineering constraints

- OBS accepts multiple camera pixel formats. Before image segmentation, convert or access frames safely and preserve correct timing.
- Native OBS filters need to handle source lifecycle, resolution changes and threading correctly.
- Model selection is open: the browser MediaPipe Tasks Vision JavaScript/WASM runtime cannot simply be loaded in a C module.
- A native-compatible runtime must be selected, with model format compatibility, licenses, CPU/GPU delegation and distribution reviewed.
- Person segmentation output must be composited with premultiplication/straight-alpha handling appropriate for OBS.
- Keep mask work at Balanced-like resolution initially to preserve the performance gains observed in the browser prototype.
- The original JS implementation remains a reference for effect controls and visual quality, not direct native source code.

## GitHub Actions: Windows build for beginners

The repository has a workflow named **Build CamLayer OBS plugin (Windows)** under `.github/workflows/build-native-windows.yml`. It uses the official OBS plugin template to supply its Windows build environment.

1. Open https://github.com/RobertAhlin/camlayer/actions
2. Select **Build CamLayer OBS plugin (Windows)** from the workflows list.
3. Select **Run workflow**, choose the `main` branch, then select **Run workflow**.
4. Open the new run. Green means the steps succeeded; red means we must inspect the failed step and fix the source or build configuration.
5. After a successful run, open **Artifacts** and download `camlayer-obs-windows-x64-bootstrap`.

The workflow also starts automatically after changes to `native/obs-plugin/` or to the workflow file itself.

**Do not install the artifact yet.** The first output is a build-test of a pass-through filter, not a functioning AI background-removal plugin. Check with IT before attempting to load unsigned or custom DLL plugins on a managed PC.

A GitHub Actions workflow may require enabling Actions in repository settings if disabled. It uses GitHub-hosted Windows build machines; nothing must be installed locally to trigger a build.

## Build and deployment

This directory deliberately contains **no claim of a verified Windows package**. The GitHub workflow now attempts a Windows build using the official OBS plugin template; it has not yet been confirmed successful. First verify the build in CI, then test the binary on a permitted machine. `find_package(libobs CONFIG REQUIRED)` needs an actual libobs CMake package path.

The target user's Windows computer is IT-managed and may prohibit installation, even for a compiled ZIP plugin. Obtain approval before copying plugins to protected OBS directories. GitHub Actions can build the code remotely, but cannot override local security policies.

## Next technical milestones

1. Establish a Windows CI build and fix any API/toolchain integration differences against a pinned OBS version.
2. Verify the filter appears in OBS and passes through video without crashes.
3. Choose and integrate a native segmentation runtime.
4. Convert capture frames and apply the alpha mask without blocking OBS rendering.
5. Port and benchmark mask settings, quality presets, and stabilization.
6. Package, document, and test with multiple camera formats and OBS versions.
