# Third-party code

All of the following are permissive and compatible with this project's GPL-3.0
licence.

## Vendored in `third_party/`

| Library | Author | Licence | Upstream |
|---|---|---|---|
| **cgltf** | Johannes Kuhlmann | MIT | https://github.com/jkuhlmann/cgltf |
| **stb_image** | Sean Barrett | MIT / public domain | https://github.com/nothings/stb |
| **meshoptimizer** | Arseny Kapoulkine | MIT | https://github.com/zeux/meshoptimizer |

Only the simplification path of meshoptimizer is compiled in: `simplifier.cpp`,
`indexgenerator.cpp`, `vcacheoptimizer.cpp`, `vfetchoptimizer.cpp`,
`allocator.cpp`, `quantization.cpp`.

## Fetched at build time, not redistributed here

| Component | Licence | Upstream |
|---|---|---|
| **Resolume FFGL SDK** | BSD 3-clause (FreeFrame) | https://github.com/resolume/ffgl |
| **GLEW** (Windows only, bundled inside the SDK) | Modified BSD / MIT | https://github.com/nigels-com/glew |

CMake clones the FFGL SDK into `build/_deps` during configuration. Nothing from
it is committed to this repository.
