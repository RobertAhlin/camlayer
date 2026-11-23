# Third-party components and distribution notices

CamLayer-authored source code is licensed under GPL-3.0-only. That license **does not automatically cover** third-party model weights, libraries, datasets or demo photographs.

Before making a downloadable public OBS plugin, review the upstream licenses, attributions, patent terms and redistribution conditions of each bundled component.

| Component | Current use | Upstream licensing notes | Distribution status |
| --- | --- | --- | --- |
| OBS Studio / libobs | Native filter host | OBS Studio's source is GPL-2.0-or-later; verify the exact binary and plugin linking obligations in the target build | Runtime external to CamLayer |
| MediaPipe Tasks Vision | Browser demo | Google MediaPipe source uses Apache-2.0; individual assets need separate verification | Loaded from external hosts for browser demo |
| Selfie Segmenter Landscape TFLite weights | Browser demo | Verify model asset terms and attribution for the specific downloadable TFLite file | Downloaded at runtime; not included in native plugin |
| ONNX MediaPipe Selfie Segmentation candidate | CI native AI probe | Model repository lists Apache-2.0; check model card, provenance and any additional restrictions | Downloaded into GitHub Actions only |
| ONNX Runtime 1.24.2 | CI native AI probe | Microsoft ONNX Runtime source is MIT-licensed; include required copyright and license notices if bundled | CI dependency, not yet included in OBS plugin |
| Robust Video Matting (RVM) code | Planned optional video-matting engine | Upstream RVM repository has GPL-3.0; preserve notices and provide corresponding source where applicable | Not integrated or distributed yet |
| RVM pretrained weights | Planned model asset | **Verify actual weight licensing, provenance and redistribution rights separately before bundling** | Not bundled |
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
