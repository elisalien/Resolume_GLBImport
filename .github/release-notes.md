An FFGL **source** plugin that loads a `.glb` / `.gltf` model straight into a
Resolume clip — decimating the mesh, playing the animations baked into the file,
and putting every parameter under Resolume's audio analysis.

Not an effect layered onto a clip. A real source you drop into an empty slot.

## Download

| Platform | File | Notes |
|---|---|---|
| Windows 10/11, 64-bit | `GlbSource-<version>-win64.zip` | run `installer.bat` |
| macOS 10.15+ | `GlbSource-<version>-macos-universal.zip` | run `installer.command` |

The macOS build is a universal binary — Apple Silicon and Intel in one bundle.
Resolume 7.11+ runs natively on Apple Silicon and refuses Intel-only plugins.

**Close Resolume before installing.** Plugins are only scanned at startup.

### Windows

Unzip, run `installer.bat`. It does two things a manual copy does not:

- checks for the Visual C++ 2015-2022 runtime the plugin links against. Without
  it Resolume skips the plugin **silently, with no error message** — far and away
  the most common reason a working FFGL plugin appears to do nothing. It downloads
  it from Microsoft if missing.
- resolves your real Documents folder, including when it is redirected to
  OneDrive, where `%USERPROFILE%\Documents` points at the wrong place.

### macOS

Unzip, then **right-click `installer.command` → Open** and confirm. macOS
quarantines anything that arrives inside a downloaded archive, and a plain
double-click will be refused.

The bundle is not code-signed or notarized. The installer strips the quarantine
attribute, without which Resolume fails to load it — again with no visible error.

## Then

**Effects panel → Sources tab → GLB Model Source** → drag into a clip →
**Model File** → pick your `.glb`.

Prefer `.glb` over `.gltf` for live work: it packs geometry and textures into one
file, so there is nothing to lose track of. A `.gltf` looks for its `.bin` and its
textures next to itself.

## What it does

**Loads real glTF.** Full node hierarchies, PBR metallic-roughness with base
colour, normal, metallic-roughness and emissive maps, external and base64 buffers,
embedded or sidecar textures, `OPAQUE` and `MASK` alpha.

**Plays the file's animations.** Node animation in `LINEAR`, `STEP` and
`CUBICSPLINE`, with GPU skinning up to 128 joints. `Anim Speed` runs from -4 to 4,
so backwards is a parameter, not a trick.

**Decimates on load.** `Mesh Density` rebuilds the index buffers once, when you
move it — not every frame, so it costs nothing during a set. The readout under the
slider is the real triangle count.

**Four render modes, stackable.** Shaded PBR, flat low-poly, unlit, wireframe-only
— plus wireframe and point-cloud overlays on top of any of them. Wireframe
thickness is computed from barycentric coordinates in a geometry shader, because
`glLineWidth` is capped at 1 px in a core profile and a one-pixel line disappears
in projection.

**Reacts to sound two ways.** The plugin takes the host's FFT and applies its own
routings to scale, spin, animation speed and displacement — nothing to wire, it
works the moment a clip plays. On top of that, every parameter stays available to
Resolume's own per-parameter Audio Analysis, with L/M/H bands, gain, fall and
direction. The two stack.

## Requirements

- Resolume Arena or Avenue **7.4.1+**. The animation-clip dropdown is filled at
  runtime through FFGL's dynamic option elements, which arrived in that version.
  Older builds load the plugin but leave the list on generic labels.
- A GPU with **OpenGL 4.1** and geometry shaders — anything since roughly 2012.

## Known limitations

- **Morph targets / shape keys are ignored.** Meshes render in their base pose.
- **Model paths are stored absolute.** A composition saved on one machine opens
  elsewhere with an empty model slot: ship the `.glb` files alongside it.
- Cameras and lights from the file are not used — the plugin has its own.
- `BLEND` alpha is drawn as opaque; no depth sorting.
- No KHR extensions beyond metallic-roughness.

## Verifying what you downloaded

`SHA256SUMS.txt` is attached to this release. The Windows DLL is a 64-bit PE32+
exporting `plugMain`; the macOS bundle is a fat Mach-O carrying both an `x86_64`
and an `arm64` slice — `lipo -archs` on the binary inside will confirm it.

Both binaries are built by GitHub Actions from the tagged source in this
repository, on `windows-latest` and `macos-latest` runners. Nothing is compiled
on a private machine.

## Source and licence

GPL-3.0-or-later. Build it yourself with CMake 3.20 and either MSVC or the Xcode
command line tools — the Resolume FFGL SDK is fetched automatically. See the
[README](../blob/main/README.md) for the full parameter reference and the design
notes, and the [changelog](../blob/main/CHANGELOG.md) for what changed.
