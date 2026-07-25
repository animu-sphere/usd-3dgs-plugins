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
| RDF→RUB reference-frame conversion | supported | shared `FlipYZAxes` (ADR 0001): position/quaternion/SH sign flips verified against independent tables and the cross-format equivalence suite |
| Opacity | supported | numerically stable sigmoid to `[0,1]` |
| SH degrees 0-3 | supported | exact-coefficient fixtures at every degree; corpus assets at degree 3; degree 4 reported unsupported (`GSPLY-E017`), not malformed |
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
| Native-RUB reference frame (no conversion) | supported | SPZ's RUB convention is the model frame (ADR 0001); dequantized verbatim, verified through the decoder and USD |
| Extension records, antialiased flag | supported (ignored) | preserved by the reader, ignored by the decoder with warnings `GSPZ-W001`/`W002` |
| Real trained SPZ assets | supported | committed 8,192-Gaussian corpus (Scaniverse, CC0) at degree 3, checked semantically by the smoke test |
| Cross-format equivalence | supported | synthetic triples encode one source model into PLY, SPZ, and SOG; `gaussian_ply_spz_equivalence` and `gaussian_ply_sog_equivalence` compare every model attribute against the lossless PLY side at documented quantization-aware tolerances, covering SPZ v2/v3 and both SOG profiles — see [EQUIVALENCE.md](EQUIVALENCE.md) |
| Metadata-only read | supported | `Read(metadataOnly=true)` authors the contract from the container header only |
| Stable diagnostics | supported | `GSPZ-E***`/`GSPZ-W***` codes with a machine-readable catalog cross-checked by the smoke test |

## SOG input and semantic mapping

| Capability | Status | Evidence / behavior |
| --- | --- | --- |
| SOG v2 bundled `.sog` (ZIP) | supported | container reader walks the archive's central directory (vendored miniz); reader and decoder fixtures for stored *and* DEFLATE-compressed entries |
| SOG v2 unbundled `meta.json` | supported | companion WebP planes load through the asset resolver, anchored on the layer's own path; a fixture directory proves both layouts author the same stage |
| SOG v1 (no `version`, per-channel `mins`/`maxs`) | unsupported | rejected with the specific unsupported-version diagnostic `GSSOG-E003`, not as corruption |
| Streamed SOG (`lod-meta.json`, chunks, LOD) | unsupported | not read; SOG M2-M4 ([release plan](../roadmap/release-plan.md)) |
| Lossless WebP property planes | supported | decoded through the vendored libwebp v1.6.0 decoder subset |
| Lossy WebP property planes | unsupported | rejected as `GSSOG-E009` rather than decoded approximately, because lossy positions would be silently wrong |
| Position (16-bit split precision, log domain) | supported | `means_l`/`means_u` codes remapped through the per-axis log range and the inverse-log transform; known-value fixtures |
| Scale (log-domain codebook) | supported | `exp(codebook[byte])`, strictly positive; a used entry that leaves float range is rejected (`GSSOG-E011`) |
| Rotation (smallest-three + tag) | supported | the three stored bytes fill the non-dropped scalar-first slots; a tag outside 252-255 fails with `GSSOG-E013` rather than becoming identity |
| Opacity (`sh0` alpha) | supported | `byte/255`, already post-sigmoid |
| DC (`sh0` codebook) | supported | raw band-0 coefficients, no color transform |
| SH rest (palette + centroids + codebook) | supported | labels resolve to centroid texels whose RGB are per-channel codebook indices; a label past the palette yields zero coefficients with one aggregated warning (`GSSOG-W001`) |
| SH degrees 0-3 | supported | `bands` 1-3 are degrees 1-3 and no `shN` is degree 0, so the whole format range is inside the model's; no degree-ceiling rejection exists |
| RDF→RUB reference-frame conversion | supported | SOG stores PLY-native Graphdeco columns, so the shared `FlipYZAxes` applies exactly as for PLY (ADR 0001, [SOG_MAPPING.md §5](SOG_MAPPING.md)); pinned by the equivalence triples |
| Zero-Gaussian file (`count: 0`) | supported rejection | well-formed SOG, but rejected with `GSSOG-E012`: the shared model requires at least one Gaussian and no stage may misrepresent its source |
| `.json` routing | supported | `.json` is registered for the unbundled layout, and claimed only when a bounded prefix parses as a SOG v2 `meta.json`; unrelated JSON is declined ([SOG_FORMAT.md §6](SOG_FORMAT.md)) |
| Metadata-only read | supported | `Read(metadataOnly=true)` authors the contract from `meta.json` alone, decoding no plane |
| Real trained SOG assets | unverified | no corpus asset committed yet; the smoke test validates any asset placed in `tests/corpus/` against its provenance record ([current.md](../roadmap/current.md) workstream 9) |
| Stable diagnostics | supported | `GSSOG-E***`/`GSSOG-W***` codes with a machine-readable catalog cross-checked against the sources in both directions |

## USD authoring

`gaussian-ply`, `gaussian-spz`, and `gaussian-sog` all author through the shared
`libs/gaussian-usd` writer, so the rows below are identical for every format.

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
| Write/export to any Gaussian format | unsupported | `WriteToFile` reports read-only behavior (`GSPLY-E203`/`GSPZ-E201`/`GSSOG-E201`) |
| Animated Gaussians | unsupported | static arrays only |
| Multiple clouds/cameras | unsupported | one `/Asset/Splat`, no camera import |

## Lifecycle

| Capability | Status |
| --- | --- |
| Standalone OST bundle build | supported |
| Plain root CMake composition | supported; Release build and all thirteen tests (core unit + decoder kit, gaussianUsd, PLY decoder + smoke, SPZ reader + decoder + smoke, SOG reader + decoder + smoke, PLY/SPZ and PLY/SOG equivalence) locally green |
| Workspace plain-library dependency | supported and validated by `ost plugin test --workspace` |
| Source L0-L5 verification | supported; locally green |
| Target-specific package | supported; locally generated and tested |
| Package-origin L0-L4 | supported; locally green |
| Package-origin L5 | skipped on the CI-pinned OST 0.18.0 (the golden is not staged); passes locally on OST 0.20.0 for `gaussian-ply` and `gaussian-sog` — see [current.md](../roadmap/current.md) |
| Windows/macOS/Linux generated CI | declared and validated; hosted execution pending |
| Hydra Gaussian renderer | unsupported here; owned by the sibling project [hydra-merlin](https://github.com/animu-sphere/hydra-merlin) ([release plan](../roadmap/release-plan.md) v0.9.0) |
