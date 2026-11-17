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

The PLY reader uses vendored tinyPLY 3.0. See the
[third-party notice](../../THIRD_PARTY_NOTICES.md#tinyply-30) for the pinned
revision and local parser fixes.

| Capability | Status | Evidence / behavior |
| --- | --- | --- |
| ASCII PLY | supported | C++ decoder, Python stage-open, and OST golden fixture |
| Binary little-endian PLY | supported | generated binary fixture and decoder/stage assertions |
| Binary big-endian PLY | unverified | no fixture or support claim |
| Gaussian dialect detection | supported | Gaussian signature check; ordinary mesh fixture rejected. See [PLY dialects](PLY_DIALECTS.md) for exporter evidence. |
| SuperSplat compressed PLY | supported | binary little-endian with the 18-property `chunk`, SH degrees 0-3, full and metadata-only reads, routed by header signature inside `gaussian-ply`. Hand-coded decoder fixtures, an index-by-index equivalence suite against lossless PLY, and committed CC0 SplatTransform 3.11.2 conversions matched to their sources; locally verified on SuperSplat 2.4.1-2.7.1 exports up to 1.94M Gaussians. Mapping and quantization loss in [COMPRESSED_PLY_MAPPING.md](COMPRESSED_PLY_MAPPING.md); producers in [PLY dialects](PLY_DIALECTS.md#observed-compressed-producers) |
| Compressed PLY variants | unsupported | ASCII or big-endian (`GSPLY-E020`) and the 12-property colourless `chunk` (`GSPLY-E021`) are rejected with specific diagnostics |
| Compressed PLY render-mode tags | supported (ignored) | `SplatRenderMode: mip \| 2dgs` and `postshot.anti_aliasing=1` produce warning `GSPLY-W004`; the Gaussians are authored as ordinary 3DGS splats |
| `.splat` and `.ksplat` | unsupported | No registered importer for either format |
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
| Metadata-only read | supported | `Read(metadataOnly=true)` authors the stage contract from the header only |
| File-format arguments | supported | `shDegree`, `opacityThreshold`, `scaleMultiplier` with validated ranges and tests |
| Stable diagnostics | supported | `GSPLY-E***`/`GSPLY-W***` codes with a machine-readable catalog shipped in the plugin resources |

## SPZ input and semantic mapping

| Capability | Status | Evidence / behavior |
| --- | --- | --- |
| SPZ container versions 1-3 (gzip) | supported | C++ container reader and decoder fixtures for each version |
| SPZ version 4 (Zstandard) | supported | per-attribute zstd streams decoded by the vendored Zstandard v1.5.7 decoder ([ADR 0002](../adr/0002-vendored-zstd-decoder.md)); reader fixtures for every structural rule in [SPZ_FORMAT.md §4](SPZ_FORMAT.md), a v4 rewrap of the v3 decoder fixture that must decode bit-identically, a reference-written v3/v4 pair from SplatTransform that must decode to identical clouds, and v4 equivalence pairs |
| SPZ versions above 4 | unsupported | rejected with the specific unsupported-version diagnostic `GSPZ-E003` |
| Declared coordinate frame (`SPZ_ADOBE_coordinate_system`) | RUB only | a record that declares RUB or leaves the frame unspecified is honored; any other frame is refused with `GSPZ-E016` in every container version, never imported misoriented ([SPZ_FORMAT.md §5](SPZ_FORMAT.md)) |
| Position (v1 float16, v2-v4 24-bit fixed point) | supported | decoder fixtures with known values; non-finite float16 rejected (`GSPZ-E012`) |
| Scale (8-bit log) | supported | `exp(byte/16 - 10)`, strictly positive |
| Rotation first-three (v1/v2) and smallest-three (v3/v4) | supported | per-version fixtures pin reconstruction and normalization |
| Opacity (8-bit) | supported | `byte/255`, already in `[0,1]` |
| DC and SH rest (8-bit quantized) | supported | dequantized directly into the model's Gaussian-major RGB triples (no transpose needed) |
| SH degrees 0-3 | supported | decoder fixtures at degrees 0, 1, and 3; degree 4 reported unsupported (`GSPZ-E011`), not malformed |
| Native-RUB reference frame (no conversion) | supported | SPZ's RUB convention is the model frame (ADR 0001); dequantized verbatim, verified through the decoder and USD |
| Extension records, extension frames, antialiased flag | supported (ignored) | records parsed by type and length in both generations, v4 skippable frames bounded but not decompressed; ignored by the decoder with warnings `GSPZ-W001`/`W002`, except the coordinate-system record above |
| Real trained SPZ assets | supported | committed 8,192-Gaussian corpus (Scaniverse, CC0) at degree 3, checked semantically by the smoke test |
| Cross-format equivalence | supported | synthetic triples encode one source model into PLY, SPZ, and SOG; `gaussian_ply_spz_equivalence` and `gaussian_ply_sog_equivalence` compare every model attribute against the lossless PLY side at documented quantization-aware tolerances, covering SPZ v2-v4 and both SOG profiles — see [EQUIVALENCE.md](EQUIVALENCE.md) |
| Metadata-only read | supported | `Read(metadataOnly=true)` authors the contract from the container header only; for v4 it also validates the plaintext extension zone and table of contents |
| Stable diagnostics | supported | `GSPZ-E***`/`GSPZ-W***` codes with a machine-readable catalog cross-checked by the smoke test |

## SOG input and semantic mapping

| Capability | Status | Evidence / behavior |
| --- | --- | --- |
| SOG v2 bundled `.sog` (ZIP) | supported | container reader walks the archive's central directory (vendored miniz); reader and decoder fixtures for stored *and* DEFLATE-compressed entries |
| SOG v2 unbundled `meta.json` | supported | companion WebP planes load through the asset resolver, always anchored on the layer's own resolved path; a fixture directory proves both layouts author the same stage; per-case resolver behavior and known gaps in [RESOLVER_MATRIX.md](RESOLVER_MATRIX.md) |
| SOG v1 (no `version`, per-channel `mins`/`maxs`) | unsupported | rejected with the specific unsupported-version diagnostic `GSSOG-E003`, not as corruption |
| Streamed SOG (`lod-meta.json`, chunks, LOD) | unsupported | no chunk or LOD import; see the [composition backlog](../roadmap/backlog.md#other-format-candidates) |
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
| Real trained SOG assets | supported | committed 8,192-Gaussian corpus (`yashica-t4`, `leica-sofort`) at degree 3, checked semantically by the smoke test |
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

See [supported configurations](SUPPORTED_CONFIGURATIONS.md) for the supported
build and package targets, the [installation guide](../guides/INSTALL.md)
for consumer checks, and the [release records](../releases/) for verified
combinations and results.

## Hydra rendering

All three importers author `ParticleField3DGaussianSplat`. Stage inspection
works independently of the active Hydra delegate. Visible splats require a
delegate that implements that schema; the stock hdStorm path is not a splat
rendering target for this project.

The experimental [Merlin Gaussian PLY host session](https://github.com/animu-sphere/hydra-merlin/blob/ad4bc6a44b3e6c01c536366b1971a62d9d88f659/formations/gaussian-host-session/README.md#verification)
verifies a synthetic Graphdeco PLY using digest-pinned public packages on
Windows x86_64. Use its pinned `lookdev` Formation to reproduce the preview.
The record covers the exact host, runtime and delegate; it does not establish
SPZ or SOG rendering support. The `usd` aggregate product is covered by the
separate [plugin release record](../releases/v0.9.0.md).
