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
| SH degree 0 | supported | DC-only fixture |
| SH degree 1 | supported | channel-major rest-coefficient reconstruction fixture |
| Higher valid SH degrees | supported by general mapping | degree inferred from square coefficient count; expand fixture corpus before release |
| Real 139,410-Gaussian degree-3 sample | locally verified | external cactus sample opened as `ParticleField3DGaussianSplat`; not vendored pending license/provenance review |
| Missing/non-contiguous SH data | supported rejection | semantic decoder rejects invalid layouts |
| Non-finite semantic values | supported rejection | core and decoder validation |
| Unknown extra properties | supported | ignored with one aggregated warning; common `nx/ny/nz` placeholders are silent |
| Property aliases | unsupported | canonical Graphdeco names only |
| Arbitrary point-cloud/mesh PLY | unsupported | rejected as non-Gaussian |

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
| Hydra Gaussian renderer | unsupported; outside this repository |
