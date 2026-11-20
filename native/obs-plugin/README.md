# Native CamLayer OBS Filter

**Status:** The original pass-through bootstrap was successfully compiled on Windows via GitHub Actions. A new GPU alpha diagnostic has now been added, but its updated build and OBS behavior are not yet verified. AI person segmentation is not yet integrated.

This folder is the starting point for a native OBS Studio plugin intended to work as a filter on a normal **Video Capture Device** source, without a second camera connection or a Chrome window.

## Files

- `src/plugin-main.c`: registers a GPU video effect filter with an optional diagnostic alpha ellipse. With the diagnostic disabled, the source is passed through visually unchanged.
- `data/locale/en-US.ini`: filter labels and notice.
- `data/ellipse_alpha.effect`: shader drawing an alpha-masked ellipse to confirm actual OBS transparency.
- `CMakeLists.txt`: initial libobs-based module target.

## Intended OBS user workflow

1. Add a Video Capture Device as usual.
2. Open its **Filters** window.
3. Add **CamLayer AI Background Removal** under **Effect Filters** or the appropriate asynchronous video filter category.
4. Configure threshold, softness, cleanup and stability.

**This is the eventual AI workflow, not a working AI feature yet.** The present version has an **optional ellipse diagnostic alpha effect** but does not identify a person.

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

The first pass-through Windows build was successful in GitHub Actions run #2. The GPU alpha diagnostic is a later code change: verify its new build and artifact independently before installing. Build success alone does not confirm that OBS can load the filter or render correct transparency. `find_package(libobs CONFIG REQUIRED)` needs an actual libobs CMake package path.

The target user's Windows computer is IT-managed and may prohibit installation, even for a compiled ZIP plugin. Obtain approval before copying plugins to protected OBS directories. GitHub Actions can build the code remotely, but cannot override local security policies.

## Next technical milestones

1. Confirm the latest GPU diagnostic Windows CI build and bundled effect file.
2. On an authorized test machine, confirm filter registration and that toggling the ellipse produces real transparency without Chroma Key.
3. Choose and integrate a native segmentation runtime.
4. Convert capture frames and apply the alpha mask without blocking OBS rendering.
5. Port and benchmark mask settings, quality presets, and stabilization.
6. Package, document, and test with multiple camera formats and OBS versions.

## First alpha diagnostic test

Once the updated artifact builds successfully, and only where installing a custom OBS plugin is authorized:

1. Install the package's `camlayer-obs` folder in OBS's recognized per-user plugin location, retaining its `bin/64bit` and `data` folder layout.
2. Restart OBS, add a normal Video Capture Device and place a color/image source behind it.
3. Add the CamLayer filter from the source's Effect Filters list.
4. Enable **Diagnostic transparency test (ellipse)**. The outer area should become transparent, revealing the background behind the camera.
5. Disable the toggle. The full camera should reappear.

The ellipse intentionally does not follow the person. Mask threshold, softness, cleanup and stability sliders are present for future AI integration but are not operational yet in the native plugin.

**Do not test by copying source C files or CMake files into OBS.** Use only a verified built artifact, and follow the device organization's IT rules.
