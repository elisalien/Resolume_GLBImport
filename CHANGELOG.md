# Changelog

## v1.0.0

First release.

- Loads `.glb` / `.gltf` into a Resolume clip through a native file-browser parameter
- Mesh decimation on load, welded on position so seam-split exports actually simplify
- glTF node animations (LINEAR / STEP / CUBICSPLINE) with GPU skinning up to 128 joints
- Render modes: shaded PBR, flat low-poly, unlit, wireframe-only, with wireframe
  and point-cloud overlays on top of any of them
- Built-in FFT routing to scale, spin, animation speed and displacement, on top of
  Resolume's own per-parameter audio analysis
- Windows x64 and universal macOS builds
