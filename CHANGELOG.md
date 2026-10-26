# Changelog

## v1.1.0

No more freeze when a clip is triggered. v1.0 parsed the file, decoded the
textures, decimated and uploaded everything inside one frame of Resolume's render
thread, which stalled the output for 0.7 to 2+ seconds on 100 MB files.

- Parsing, texture decoding, mipmaps and decimation moved to worker threads
- GPU upload spread over frames under a ~5 ms per-frame budget
- Process-wide asset cache: retriggering a clip, or the same file in several
  clips, skips the parse (invalidated when the file changes on disk)
- `Mesh Density` simplifies in the background and memoises recent values, so
  moving the slider or retriggering a decimated clip no longer hitches
- Shader programs compiled once per GL context instead of once per trigger
- Mipmaps built on the CPU (sRGB-correct) instead of `glGenerateMipmap`
- Stopping a clip during a load no longer waits for the load to finish

Measured with a 95 MB / 18-texture `.glb` on an RTX 5090 laptop: worst frame on
trigger went from ~1.8–2.3 s to ~7 ms; a cached retrigger is visible in ~0.3 s.

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
