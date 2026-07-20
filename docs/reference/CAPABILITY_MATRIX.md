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

## SPZ input and semantic mapping

| Capability | Status | Evidence / behavior |
| --- | --- | --- |
| SPZ container versions 1-3 (gzip) | supported | C++ container reader and decoder fixtures for each version |
| SPZ version 4 (ZSTD) | unsupported | rejected with the specific unsupported-version diagnostic `GSPZ-E003` |
| Position (v1 float16, v2/v3 24-bit fixed point) | supported | decoder fixtures with known values; non-finite float16 rejected (`GSPZ-E012`) |
| Scale (8-bit log) | supported | `exp(byte/16 - 10)`, strictly positive |
| Rotation first-three (v1/v2) and smallest-three (v3) | supported | per-version fixtures pin reconstruction and normalization |
| Opacity (8-bit) | supported | `byte/255`, already in `[0,1]` |
| DC and SH rest (8-bit quantized) | supported | dequantized directly into the model's Gaussian-major RGB triples (no transpose needed) |
| SH degrees 0-3 | supported | decoder fixtures at degrees 0, 1, and 3; degree 4 reported unsupported (`GSPZ-E011`), not malformed |
| RUB→RDF reference-frame conversion | supported | position/quaternion/SH sign flips verified through the decoder and USD |
| Extension records, antialiased flag | supported (ignored) | preserved by the reader, ignored by the decoder with warnings `GSPZ-W001`/`W002` |
| Real trained SPZ assets | supported | committed 8,192-Gaussian corpus (Scaniverse, CC0) at degree 3, checked semantically by the smoke test |
| PLY/SPZ cross-format equivalence | supported | synthetic pairs encode one source model into both formats; `gaussian_ply_spz_equivalence` compares every model attribute at documented quantization-aware tolerances, covering SPZ v2 and v3 — see [EQUIVALENCE.md](EQUIVALENCE.md) |
| Metadata-only read | supported | `Read(metadataOnly=true)` authors the contract from the container header only |
| Stable diagnostics | supported | `GSPZ-E***`/`GSPZ-W***` codes with a machine-readable catalog cross-checked by the smoke test |

## USD authoring

Both `gaussian-ply` and `gaussian-spz` author through the shared
`libs/gaussian-usd` writer, so the rows below are identical for either format.

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
| PLY/SPZ write/export | unsupported | `WriteToFile` reports read-only behavior (`GSPLY-E203`/`GSPZ-E201`) |
| Animated Gaussians | unsupported | static arrays only |
| Multiple clouds/cameras | unsupported | one `/Asset/Splat`, no camera import |

## Lifecycle

| Capability | Status |
| --- | --- |
| Standalone OST bundle build | supported |
| Plain root CMake composition | supported; Release build and all eight tests (core, gaussianUsd, PLY decoder + smoke, SPZ reader + decoder + smoke, PLY/SPZ equivalence) locally green |
| Workspace plain-library dependency | supported and validated by `ost plugin test --workspace` |
| Source L0-L5 verification | supported; locally green |
| Target-specific package | supported; locally generated and tested |
| Package-origin L0-L4 | supported; locally green |
| Package-origin L5 | currently skipped because the golden is not staged by OST |
| Windows/macOS/Linux generated CI | declared and validated; hosted execution pending |
| Hydra Gaussian renderer | unsupported here; owned by the sibling project [hydra-merlin](https://github.com/animu-sphere/hydra-merlin) ([release plan](../roadmap/release-plan.md) v0.9.0) |
