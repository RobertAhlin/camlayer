# Third-party components and distribution notices

CamLayer-authored source code is licensed under GPL-3.0-only. That license **does not automatically cover** third-party model weights, libraries, datasets or demo photographs.

Before making a downloadable public OBS plugin, review the upstream licenses, attributions, patent terms and redistribution conditions of each bundled component.

| Component | Current use | Upstream licensing notes | Distribution status |
| --- | --- | --- | --- |
| OBS Studio / libobs | Native filter host | OBS Studio's source is GPL-2.0-or-later; verify the exact binary and plugin linking obligations in the target build | Runtime external to CamLayer |
| MediaPipe Tasks Vision | Browser demo | Google MediaPipe source uses Apache-2.0; individual assets need separate verification | Loaded from external hosts for browser demo |
| Selfie Segmenter Landscape TFLite weights | Browser demo | Verify model asset terms and attribution for the specific downloadable TFLite file | Downloaded at runtime; not included in native plugin |
| ONNX MediaPipe Selfie Segmentation candidate | Native MediaPipe inference | Model repository lists Apache-2.0; check model card, provenance and any additional restrictions | Bundled in Windows Actions test artifact |
| ONNX Runtime 1.24.2 | Native OBS plugin runtime | Microsoft ONNX Runtime uses the MIT license; include required copyright and license notices in a public package | Bundled as onnxruntime.dll in Windows Actions test artifact |
| Robust Video Matting (RVM) code | Native RVM inference integration | Upstream RVM repository uses GPL-3.0; preserve upstream notices and provide corresponding source where required | RVM inference is implemented in the plugin |
| RVM pretrained weights | Official MobileNetV3 FP32 ONNX model | **Verify model-weight license and redistribution rights independently of the upstream source-code GPL before publishing a release** | Downloaded, SHA256 checked and included in GitHub Actions test artifact |
| Test portrait | CI quality fixture | Referenced from another public project with Pexels attribution; validate photo/license permissions and reproduce attribution if redistributed | CI download only; not committed to this repository |

## Open-source policy

1. CamLayer's own code is GPL-3.0-only, with the complete license text in [LICENSE](LICENSE).
2. Contributions to CamLayer should be compatible with GPL-3.0-only unless clearly marked and reviewed.
3. Do not copy upstream model source, model weights, or training materials into this repository without checking their exact licenses.
4. Keep original notices, copyright statements and required license texts when distributing compatible third-party software.
5. Public distributions must include a suitable offer or access to corresponding CamLayer source code and other material required by the GPL.
6. Do not assume merely separating AI engines into folders removes GPL requirements for a combined or derivative work.
7. Do not claim any third-party model weights are GPL-licensed unless the asset's actual license confirms this.
8. The browser demo and native OBS plugin share the same project-wide GPL licensing strategy, although third-party components retain their licenses.

This file is an engineering compliance inventory, not a substitute for legal review before public redistribution.

## Public preview release checklist (27 November 2026)

The Windows CI pipeline currently creates a **downloadable test artifact** containing `camlayer-obs.dll`, `onnxruntime.dll` and both ONNX model files. That is already a form of distribution to artifact downloaders, although artifacts expire after 14 days. Before promoting this package to a persistent, publicly advertised GitHub Release, maintainers must:

1. Verify provenance, license and explicit binary redistribution rights for the pinned **RVM MobileNetV3 FP32 ONNX weights**, independently of the RVM source repository's GPL-3.0 code license.
2. Confirm the exact MediaPipe ONNX model's license and whether attribution or additional model notices are required.
3. Include the ONNX Runtime MIT copyright/license text, CamLayer GPL license and third-party attribution files in the published ZIP, and confirm compliance with OBS/libobs obligations.
4. Link public release assets to corresponding source code at the exact commit/tag, and retain SHA256 verification for downloaded assets.
5. Check whether additional redistributable binaries packaged by the OBS template have separate license requirements.

A green CI build confirms technical tests, **not permission to redistribute every included component**. The publishing workflow requires a human acknowledgement of this review.
