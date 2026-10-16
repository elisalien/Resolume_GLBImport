# GLB Model Source

> An FFGL 2.2 source plugin for Resolume Arena and Avenue that loads `.glb` / `.gltf` 3D models straight into a clip.

![FFGL](https://img.shields.io/badge/FFGL-2.2-blue) ![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20macOS%20universal-informational) ![Stack](https://img.shields.io/badge/stack-C%2B%2B%20%7C%20OpenGL%204.1-blue) ![License](https://img.shields.io/badge/license-GPLv3-green)

**[Download the latest release](../../releases/latest)** · [Changelog](CHANGELOG.md)

## About

An FFGL 2.2 **source** plugin for Resolume Arena and Avenue that loads a
`.glb` / `.gltf` model straight into a clip, decimates its mesh on load, plays the
animations baked into the file, and exposes every knob to Resolume's audio
analysis.

Not an effect on top of a clip — a real source you drop into an empty slot.

## Features

- Loads `.glb` / `.gltf` into a Resolume clip through a native file-browser parameter.
- Mesh decimation on load, welded on position so seam-split exports actually simplify.
- glTF node animations (`LINEAR` / `STEP` / `CUBICSPLINE`) with GPU skinning up to 128 joints.
- Render modes: shaded PBR, flat low-poly, unlit, wireframe-only, with wireframe
  and point-cloud overlays on top of any of them.
- Built-in FFT routing to scale, spin, animation speed and displacement, on top of
  Resolume's own per-parameter audio analysis.
- Windows x64 and universal macOS builds.

### glTF support

Handled: `.glb` and `.gltf`, external and base64 buffers, embedded or sidecar
textures, full node hierarchies, PBR metallic-roughness (base colour, normal map,
metallic-roughness, emissive), `OPAQUE` and `MASK` alpha, node animations in
`LINEAR` / `STEP` / `CUBICSPLINE`, GPU skinning up to 128 joints per skin.

Not handled: morph targets / shape keys (the `weights` channels are ignored),
cameras and lights from the file, depth-sorted `BLEND` alpha, KHR extensions
beyond metallic-roughness.

## Getting started

### Requirements

- Resolume Arena or Avenue **7.4.1+**. The animation-clip dropdown is filled at
  runtime through FFGL's dynamic option elements, which landed in that version.
  Older builds load the plugin but leave the list stuck on generic labels.
- A GPU with **OpenGL 4.1** and geometry shader support — anything since ~2012.
- Windows x64, or macOS 10.15+. The macOS build is universal (arm64 + x86_64):
  Resolume 7.11+ runs natively on Apple Silicon and refuses Intel-only plugins.

### Install

Grab the archive for your platform from the [latest release](../../releases/latest).

**Windows** — unzip, run `installer.bat`. It checks for the Visual C++ runtime the
plugin needs and installs it if missing (without it Resolume skips the plugin
*silently*, which is the single most common reason a working FFGL plugin appears
to do nothing), then copies the DLL into `Documents\Resolume Arena\Extra Effects`,
resolving that path even when Documents is redirected to OneDrive.

**macOS** — unzip, run `installer.command` (right-click → Open the first time;
macOS quarantines anything that arrives in a downloaded archive). It clears the
quarantine attribute and copies `GlbSource.bundle` into
`~/Documents/Resolume Arena/Extra Effects`.

Close Resolume first either way: plugins are only scanned at startup.

The archives also ship a French quick guide: [`packaging/windows/LISEZMOI.md`](packaging/windows/LISEZMOI.md),
[`packaging/macos/LISEZMOI.md`](packaging/macos/LISEZMOI.md).

### Build from source

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release   # add -A x64 on Windows
cmake --build build --config Release
cmake --build build --config Release --target install-plugin
```

Needs CMake 3.20+, Git, and MSVC 2019/2022 or the Xcode command line tools. The
Resolume FFGL SDK is fetched automatically into `build/_deps`; point
`-DFFGL_SDK_DIR=/path/to/ffgl` at an existing checkout to build offline. On
Windows, `build.bat` does all three steps in one go.

Release binaries are produced by GitHub Actions from this same source — see
[`.github/workflows/release.yml`](.github/workflows/release.yml).

## Usage

**Effects panel → Sources tab → GLB Model Source** → drag into a clip →
**Model File** → pick your `.glb`.

### Parameters

**Model** — `Model File` (native file browser, filtered to glb/gltf), `Reload`,
`Mesh Density`, `Auto Fit`.

`Mesh Density` re-runs the decimation and rebuilds the index buffers once, when
you move it — not every frame. The value readout under the slider shows the real
triangle count, which is also the best way to tell whether a given mesh actually
simplifies.

**Transform** — `Scale`, `Rotation X/Y/Z`, `Spin X/Y/Z` (turns per second),
`Position X/Y`, `Camera Distance`, `Field Of View`.

**Animation** — `Clip` (populated with the animation names found in the file),
`Anim Speed` (negative runs it backwards), `Play`, `Restart`.

**Render** — `Render Mode` (Shaded / Flat Low Poly / Unlit / Wireframe Only),
`Wireframe` + width + opacity, `Points` + size, `Displace`, `Backface Cull`,
`Anti Alias` (off / 2x / 4x / 8x MSAA). Wireframe and points overlay on top of
any base mode.

**Material** — `Tint` (HSBA), `Light Yaw` / `Pitch` / `Intensity`, `Ambient`,
`Metallic`, `Roughness` (the last two multiply the glTF material values),
`Background` alpha — 0 keeps the frame transparent so the source composites over
whatever is underneath.

**Audio** — `Band` (Volume / Low / Mid / High), `Gain`, `Smooth`, and four
routings: `Audio To Scale`, `Audio To Spin`, `Audio To Anim`, `Audio To Displace`.

### Two ways to react to sound

**Built in.** The plugin requests the host's FFT (`FF_USAGE_FFT`) and applies the
four `Audio To …` routings itself. Nothing to wire — it works the moment a clip
plays.

**Resolume's own.** Right-click any parameter → Audio Analysis, with L/M/H bands,
gain, fall and direction. Finer, and available on *every* parameter including the
ones the built-in routings don't cover — Field Of View, Wire Width, Roughness.
The two stack.

### Known rough edges

**Model paths are absolute.** A composition saved on one machine opens elsewhere
with an empty model slot. Ship the `.glb` files alongside and re-point them.

**Morph targets are ignored.** If your Blender exports rely on shape keys, the
mesh renders in its base pose.

**Black model?** Either `Light Intensity` and `Ambient` are both at zero, or the
material is fully metallic with no environment to reflect — raise `Ambient` or
lower `Metallic`.

**Nothing on screen?** `Auto Fit` off with a model in Blender units (metres) and
`Camera Distance` at 3 puts it either enormous or microscopic. Turn `Auto Fit` on.

Errors and load confirmations go to the Resolume console (`Ctrl+Shift+L`, `Cmd+Shift+L` on macOS).

### How it works

**The host framebuffer has no depth attachment.** That is the trap every 3D FFGL
source hits. The plugin renders into its own FBO with colour + depth (multisampled
when AA is on), resolves it, then blits into Resolume's framebuffer with a
fullscreen triangle, restoring the full GL state on the way out as FFGL requires.

**Decimation welds on position first.** glTF exporters split vertices along
normal and UV seams. Feeding that topology straight to a simplifier locks nearly
every edge as a boundary and removes almost nothing. So the plugin builds a
position-only welded stream at load (`meshopt_generateVertexRemapMulti`),
simplifies *that*, then maps the result back onto the original vertices so
normals, UVs and skin weights survive.

**Wireframe uses barycentric coordinates from a geometry shader.** `glLineWidth`
is capped at 1 px in a core profile, so controllable, antialiased line thickness
has to be computed in the fragment shader. It matters when you are projecting.

**Colour management.** Base-colour and emissive textures upload as
`GL_SRGB8_ALPHA8` so sampling linearises them, glTF factors are already linear,
lighting runs in linear space, and the fragment shader encodes back to sRGB.

**Loading never touches OpenGL.** Parsing can fail cleanly; the GPU upload happens
on the render thread. A large `.glb` costs one frame.

Every GL entry point used has been checked against the OpenGL 4.1 core profile —
macOS's ceiling — so the same source builds and runs on both platforms.

## Project structure

```
src/ParamIds.h          parameter indices — never reorder after a comp is saved
src/Math3D.h            vec3 / quat / mat4, column-major, no dependencies
src/ColorUtils.h        Resolume HSB -> linear RGB
src/Shaders.h           all GLSL (shared vertex, PBR, wireframe GS, points, blit)
src/GltfModel.*         cgltf parsing, welding, decimation, animation, skinning
src/GlbSource.*         FFGL plugin: parameters, programs, render target, audio
src/GlbSourceRender.cpp render passes, blit, ProcessOpenGL, parameter accessors
third_party/            cgltf, stb_image, meshoptimizer (simplification path)
packaging/              Windows / macOS installers and LISEZMOI guides
.github/workflows/      CI and release builds
```

The plugin's FFGL id is `EL3D`. If you fork this into a variant meant to coexist
with the original, change that id — Resolume loads only one plugin per id. And
never reorder `ParamIds.h` once a composition has been saved: values are
serialised by index, not by name.

## Tech stack

C++, OpenGL 4.1 core (GLSL, geometry shaders), Resolume FFGL SDK 2.2, CMake, cgltf, stb_image, meshoptimizer, GitHub Actions.

## Credits & license

GPL-3.0-or-later. See [`LICENSE`](LICENSE).

Bundled third-party code and its licences are listed in
[`THIRD_PARTY.md`](THIRD_PARTY.md) — all permissive and GPL-compatible
(cgltf by Johannes Kuhlmann, stb_image by Sean Barrett, meshoptimizer by Arseny Kapoulkine). The
Resolume FFGL SDK is fetched at build time rather than vendored.

Being GPL does not prevent the plugin from being loaded by Resolume: the FFGL API
is a documented plugin interface, the same situation as the many GPL audio plugins
that load into proprietary hosts. It does mean that if you distribute a modified
version of *this plugin*, you distribute its source under the same licence.

---

<p align="center">
  Made by <a href="https://github.com/elisalien">Élisalien</a> ·
  <a href="https://gitlab.com/elisalien/catalog">Shaders, sources & software catalog</a>
</p>
