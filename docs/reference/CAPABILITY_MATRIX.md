# Capability matrix

This table describes the current tree. Planned capabilities belong in the
[roadmap](../roadmap/), not here.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| **supported** | Implemented and covered by deterministic tests. |
| **policy default** | Authored by importer convention because the source does not define it. |
| **unsupported** | Intentionally rejected or not implemented. |
| **unverified** | Code or a dependency may allow it, but the project does not claim it without a fixture and gate. |

## Input and semantic mapping

| Capability | Status | Evidence / behavior |
| --- | --- | --- |
| ASCII PLY | supported | C++ decoder, Python stage-open, and OST golden fixture |
| Binary little-endian PLY | supported | generated binary fixture and decoder/stage assertions |
| Binary big-endian PLY | unverified | no fixture or support claim |
| Gaussian dialect detection | supported | Gaussian signature check; ordinary mesh fixture rejected |
| Position | supported | `x/y/z` copied as local-space position |
| Scale | supported | stable finite `exp(scale_*)` conversion |
| Rotation | supported | scalar-first quaternion normalization; zero becomes identity with warning |
| Opacity | supported | numerically stable sigmoid to `[0,1]` |
| SH degrees 0-3 | supported | exact-coefficient fixtures at every degree; corpus assets at degree 3 |
| Property declaration order | supported | name-based resolution; scrambled-order fixture and Brush's lexicographic `f_rest` order both decode identically |
| Real trained assets | supported | committed 8,192-Gaussian corpus (Brush, CC0); locally verified against Graphdeco `garden` (5.83M), Postshot cactus series (up to 1.94M) — see [PLY_DIALECTS.md](PLY_DIALECTS.md) and [PERFORMANCE_BASELINES.md](PERFORMANCE_BASELINES.md) |
| Missing/non-contiguous SH data | supported rejection | malformed fixtures for every header-layout diagnostic |
| Non-finite semantic values | supported rejection | core and decoder validation |
| Unknown extra properties | supported | ignored with one aggregated warning; common `nx/ny/nz` placeholders are silent |
| Property aliases | unsupported | canonical Graphdeco names only; decision recorded in [PLY_DIALECTS.md](PLY_DIALECTS.md) |
| Arbitrary point-cloud/mesh PLY | unsupported | rejected as non-Gaussian |
| Metadata-only read | supported | `Read(metadataOnly=true)` authors the stage contract from the header only (~5 ms at any size) |
| File-format arguments | supported | `shDegree`, `opacityThreshold`, `scaleMultiplier` with validated ranges and tests |
| Stable diagnostics | supported | `GSPLY-E***`/`GSPLY-W***` codes with a machine-readable catalog shipped in the plugin resources |

## USD authoring

| Capability | Status | USD output |
| --- | --- | --- |
| Standard Gaussian schema | supported | `ParticleField3DGaussianSplat` |
| Asset hierarchy | supported | `/Asset/Splat` |
| Default prim | supported | `/Asset` |
| Up axis | policy default | Y |
| Linear units | policy default | 1 meter per unit |
| Extent | supported | conservative three-sigma bounds from position/scale |
| Source provenance | supported | `customData.gs` with source format, count, and SH degree |
| USDA inspection | supported | `WriteToString` delegates to USDA |
| PLY write/export | unsupported | `WriteToFile` reports read-only behavior |
| Animated Gaussians | unsupported | static arrays only |
| Multiple clouds/cameras | unsupported | one `/Asset/Splat`, no camera import |

## Lifecycle

| Capability | Status |
| --- | --- |
| Standalone OST bundle build | supported |
| Plain root CMake composition | supported; Visual Studio 2022 Release build and all three tests locally green |
| Workspace plain-library dependency | supported and validated by `ost plugin test --workspace` |
| Source L0-L5 verification | supported; locally green |
| Target-specific package | supported; locally generated and tested |
| Package-origin L0-L4 | supported; locally green |
| Package-origin L5 | currently skipped because the golden is not staged by OST |
| Windows/macOS/Linux generated CI | declared and validated; hosted execution pending |
| Hydra Gaussian renderer | unsupported here; owned by the sibling project [hydra-merlin](https://github.com/animu-sphere/hydra-merlin) ([release plan](../roadmap/release-plan.md) v0.9.0) |
