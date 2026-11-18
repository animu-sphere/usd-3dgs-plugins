# Gaussian `.splat` mapping contract

This is the normative mapping for the `gaussian-splat` bundle. It documents how
a `.splat` file becomes the format-independent
[`GaussianCloudData`](GAUSSIAN_MODEL_CONTRACT.md) and the OpenUSD values the
plugin authors. The record layout, routing, and bounds are owned by
[SPLAT_FORMAT.md](SPLAT_FORMAT.md); this document owns the semantic decode. It
mirrors [PLY_MAPPING.md](PLY_MAPPING.md), [SPZ_MAPPING.md](SPZ_MAPPING.md), and
[SOG_MAPPING.md](SOG_MAPPING.md).

The reference for every formula is the antimatter15 `convert.py` writer and
`main.js` reader, cross-checked against SplatTransform 3.11.2
(`read-splat.ts`, `write-splat-streaming.ts`); see the
[FMT-13 spike](../reports/format-spike-splat-2026-11-19.md) §1-§3.

## 1. Record fields

| Offset | Stored | Written as |
| --- | --- | --- |
| 0 | f32 × 3 position | Graphdeco `x, y, z`, unchanged |
| 12 | f32 × 3 scale | `exp(scale_i)` of the Graphdeco log-scale |
| 24 | u8 × 3 colour | `(0.5 + SH_C0 · f_dc_i) · 255`, clamped to `[0, 255]` |
| 27 | u8 alpha | `sigmoid(opacity) · 255`, clamped to `[0, 255]` |
| 28 | u8 × 4 rotation | `q_i · 128 + 128` of the normalized `rot_0..rot_3` (`w, x, y, z`), clamped to `[0, 255]` |

`SH_C0 = 0.28209479177387814`, the band-0 SH basis constant. Writers truncate
or round the clamped 8-bit values (SPLAT_FORMAT.md §3).

## 2. Semantic conversion

| Model field | Decode | Checked |
| --- | --- | --- |
| `positions` | the three float32 values | finite, else `GSSPLAT-E006` |
| `scales` | the three float32 values; they are already linear | finite and positive, else `GSSPLAT-E007` |
| `dcCoefficients` | `(c / 255 − 0.5) / SH_C0` per channel | always finite |
| `opacities` | `a / 255` | always in `[0, 1]` |
| `rotations` | §4 | always normalizable or zero |
| `shDegree`, `restCoefficients` | 0, empty | — |

Then the decoder applies the frame conversion in §5 and the shared
`ValidateGaussianCloud`.

## 3. Colour and opacity

The DC term is the only SH information the format keeps. Decoding inverts the
colour formula exactly; the losses are the writer's:

- **Step.** One colour code is `1 / (255 · SH_C0) ≈ 0.0139` in DC, and one
  alpha code is `1 / 255` in opacity. A truncating writer is off by up to one
  step, a rounding writer by up to half.
- **Clipping.** A byte holds DC in `[−0.5 / SH_C0, +0.5 / SH_C0] ≈ ±1.7725`.
  Writers clamp values outside that range, so they decode at the boundary. In
  the committed CC0 corpus 25-32 % of the Gaussians have at least one clipped
  channel (spike §6); the equivalence suite clips the expected PLY value
  before comparing.
- **Rest SH.** Not stored. A `.splat` file always decodes at SH degree 0.
- **Opacity 0.** An alpha byte of 0 decodes to opacity 0, which the model
  admits.

## 4. Rotation

Each byte decodes as `(b − 128) / 128`, scalar first, and the quaternion is
normalized with the shared `NormalizeQuaternion`. This is the exact inverse
of `q · 128 + 128` and the rule of the reference reader. SplatTransform
decodes `b / 255 · 2 − 1` instead; the two differ by half a step, and neither
is closer for every writer (spike §6).

- **Error.** A truncating writer moves each component by up to `1/128` before
  normalization; normalizing the four-component error adds at most `2/128`,
  so each decoded component is within `0.025` of the source. A rounding writer
  halves both. Measured over 2,000,000 random rotations, the worst angle is
  1.75° for truncation and 0.89° for rounding.
- **Zero rotation.** All four bytes 128 decode to a zero quaternion, which the
  shared helper replaces with identity. The import reports how many records
  did so with `GSSPLAT-W001`.
- **Clamping.** A component of `+1` encodes as 255, which decodes to
  `127/128`; normalization restores the unit quaternion.

## 5. Coordinate system (Graphdeco RDF)

Every observed writer stores the columns a Graphdeco PLY stores, in the
right-down-front frame (spike §5). The decoder applies the shared
`FlipYZAxes` ([ADR 0001](../adr/0001-model-frame-is-rub.md)): positions
`(x, y, z) → (x, −y, −z)` and quaternions `(w, x, y, z) → (w, x, −y, −z)`.
Scales and DC are unchanged and there are no rest coefficients, so no SH sign
applies. A `.splat` file and the PLY it was made from import to the same
orientation, which the equivalence suite checks on the corpus. The import
statistics report the conversion as `rdf_to_rub`.

The frame is a convention, not a declaration in the file. A file whose writer
did not start from Graphdeco-frame data imports in that writer's frame; the
importer does not infer frames.

## 6. OpenUSD mapping

Identical to the other formats; `.splat` authors through the same
`libs/gaussian-usd` `GaussianLayerWriter`:

| Semantic value | OpenUSD attribute on `/Asset/Splat` |
| --- | --- |
| positions | `positions` |
| linear scales | `scales` |
| normalized quaternions | `orientations` |
| opacities | `opacities` |
| SH degree (always 0) | `radiance:sphericalHarmonicsDegree` |
| DC coefficients | `radiance:sphericalHarmonicsCoefficients` |

`/Asset` carries `customData.gs` with
`sourceFormat = "Gaussian Splatting SPLAT"`, `gaussianCount`, and
`shDegree`. The layer uses the policy defaults `upAxis = Y` and
`metersPerUnit = 1`; the file carries no units.

## 7. Metadata-only reads

`Read(metadataOnly=true)` authors the `/Asset` scaffold, stage metrics, and
`customData.gs` from the file size and the four-byte signature check alone:
the count is `fileSize / 32` and the SH degree is 0. No record is read, so a
value error such as a zero scale is reported only by a full read.

## 8. Diagnostics

Every error and warning starts with a stable bracketed identifier
(`[GSSPLAT-E0xx] ...`). The catalog
`plugin/resources/gaussian-splat/diagnostics.json` lists every code, and the
bundle's Python test cross-checks it against the source constants in both
directions.

| Code | Meaning |
| --- | --- |
| `GSSPLAT-E001` | Empty file |
| `GSSPLAT-E002` | Size not a multiple of 32 bytes |
| `GSSPLAT-E003` | Signature of another format (renamed file) |
| `GSSPLAT-E004` | Record count above the import limit |
| `GSSPLAT-E005` | File cannot be opened or read completely |
| `GSSPLAT-E006` | Non-finite position |
| `GSSPLAT-E007` | Zero, negative, or non-finite scale |
| `GSSPLAT-E008` | Shared model validation failed (decoder gap) |
| `GSSPLAT-E009` | Record buffer or model allocation failed |
| `GSSPLAT-E100`-`E104` | Internal misuse and USD authoring failures |
| `GSSPLAT-E201` | Writing `.splat` is unsupported |
| `GSSPLAT-W001` | Zero rotations replaced with identity |
