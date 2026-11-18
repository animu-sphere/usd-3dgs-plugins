# Format spike: `.splat` — 2026-11-19

This is the FMT-13 evaluation in [current work](../roadmap/current.md).
It records what was observed on 2026-11-19 against the named upstream
revisions and files. It is not a support claim. The
[capability matrix](../reference/CAPABILITY_MATRIX.md) does not list `.splat`.

## Summary

| Check | Result |
| --- | --- |
| Specification | There is no prose specification. The MIT reference implementation is unambiguous, and four other readers agree with it on the layout (§2). |
| Variants | One record layout, with no header or version field. Writers differ only in how they quantize: truncation or rounding, and whether they normalize first. Readers differ in how they map rotation bytes back (§3). |
| Writers | SplatTransform since 3.0.0 and SuperSplat (through SplatTransform), the antimatter15 converter and viewer, gsplat.js, and Spark. No capture app was found to export `.splat` (§4). |
| Coordinate frame | All observed writers store Graphdeco-PLY-frame data (RDF). The decoder applies `FlipYZAxes`, as PLY and SOG do. Nothing in the file declares the frame (§5). |
| Quantization | Positions and scales are float32. Colour, opacity, and rotation are 8-bit. There are no rest SH coefficients. The colour encoding clips DC: 25-32 % of the Gaussians in the committed CC0 subsets fall outside the range it can store (§6). |
| Routing | No other known plugin claims `splat`. `CanRead()` needs only the extension and the file size. `Read()` rejects the signatures of other formats with a specific diagnostic (§7). |
| Fixtures | Deterministic fixtures and CC0 corpus conversions can be generated in the repository, and an MIT reference-written file can serve as a cross-check. The public sample files have no licence (§8). |
| Bundle | A new `gaussian-splat` bundle. No new dependency (§9). |
| Recommendation | Keep `.splat` as the v0.12.0 format. No blocker was found. Scope as in §10. |

## 1. Sources

| Source | Revision | Licence |
| --- | --- | --- |
| [antimatter15/splat](https://github.com/antimatter15/splat): `convert.py`, `main.js`, `README.md` | `main`, commit `ba182b51b7c2` (2025-11-16) | MIT |
| [SplatTransform](https://github.com/playcanvas/splat-transform): `src/lib/readers/read-splat.ts`, `src/lib/writers/write-splat-streaming.ts`, `src/lib/read.ts`, `src/lib/write.ts`, `src/lib/utils/math.ts`, `test/fixtures/create-fixtures.mjs`, `test/fixtures/splat/minimal.splat` | tag `v3.11.2`, commit `6f642939dd5d` (equal to `main` on 2026-11-19) | MIT |
| [SuperSplat](https://github.com/playcanvas/supersplat): `src/import-formats.ts`, `src/file-handler.ts`, `src/splat-serialize.ts`, `package.json` | commit `8b6af56d0b58` (2026-11-17); depends on `@playcanvas/splat-transform` 3.11.2 | MIT |
| [gsplat.js](https://github.com/huggingface/gsplat.js): `src/splats/SplatData.ts`, `src/loaders/PLYLoader.ts` | commit `dbe93f6dd3de` (2026-09-23) | MIT |
| [GaussianSplats3D](https://github.com/mkkellogg/GaussianSplats3D): `src/loaders/splat/SplatParser.js` | commit `eb2fc4593e3e` (2025-10-19) | MIT |
| [Spark](https://github.com/sparkjsdev/spark): `rust/spark-lib/src/antisplat.rs`, `src/SplatLoader.ts` | commit `ffe210dc0a45` (2026-10-20) | MIT |
| [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD) `pxr/usd/sdf/fileFormat.cpp`, `pxr/usd/sdf/fileFormatRegistry.cpp` | `release` branch, commit `ee47c679abde` | Tomorrow Open Source Technology License |
| [Adobe USD-Fileformat-plugins](https://github.com/adobe/USD-Fileformat-plugins) `*/src/plugInfo.json.in` | commit `6441b97807a5` (2026-07-17) | Apache-2.0 |

The format has no written specification. The antimatter15 viewer defines it,
and its converter is the reference writer. Every fact in §2 is stated in both
the reference reader and the reference writer, and four other readers agree
with it. The layout is therefore documented by an independently implementable
reference rather than inferred from sample files, which meets the
[format selection policy](../design/DESIGN_POLICY.md#4-format-selection).
No upstream code is copied into this repository.

## 2. Record layout

A `.splat` file is a sequence of 32-byte records, one per Gaussian, with no
header, footer, or padding. All values are little-endian.

| Offset | Type | Field | Encoding |
| --- | --- | --- | --- |
| 0 | f32 × 3 | position | stored as-is |
| 12 | f32 × 3 | scale | linear standard deviation: the writer stores `exp(scale_i)` of the Graphdeco log-scale |
| 24 | u8 × 3 | colour | `(0.5 + SH_C0 · f_dc_i) · 255`, clamped to `[0, 255]`, with `SH_C0 = 0.28209479177387814` |
| 27 | u8 | alpha | `sigmoid(opacity) · 255`, clamped to `[0, 255]` |
| 28 | u8 × 4 | rotation | scalar first (`w, x, y, z`, the Graphdeco `rot_0..rot_3` order): `q_i · 128 + 128`, clamped to `[0, 255]` |

- The Gaussian count is `fileSize / 32`. SplatTransform and Spark reject a
  size that is not a multiple of 32. SplatTransform also rejects an empty
  file. The antimatter15 viewer floors the count and ignores the remainder.
- Record order has no meaning. The reference converters sort by a
  size-times-opacity importance so that progressive loading shows large
  splats first. Other writers keep the source order.
- The format carries no SH degree, no rest coefficients, no frame, no
  units, and no producer tag.

## 3. Variants

The layout has a single variant. What varies is the arithmetic writers and
readers use for the 8-bit fields.

| Implementation | Rotation write | Colour/alpha write | Rotation read |
| --- | --- | --- | --- |
| antimatter15 `convert.py` | normalize, `·128+128`, truncate | truncate | — |
| antimatter15 viewer (drag-and-drop PLY) | normalize, `·128+128`, round (`Uint8ClampedArray`) | round | `(b − 128) / 128`, not renormalized |
| SplatTransform 3.0.0-3.7.x | `·128+128` without normalizing, truncate | truncate | `b / 255 · 2 − 1`, normalized |
| SplatTransform ≥ 3.8.0 | normalize, `·128+128`, truncate | truncate | as above |
| gsplat.js | normalized on PLY import; serialize uses `(q·128+128) & 0xff` | as stored | `(b − 128) / 128` |
| GaussianSplats3D | — | — | `(b − 128) / 128` |
| Spark | clamp to `[−1, 1]`, `round(q·128) + 128` | round | `(b − 128) / 128` |

- SplatTransform 3.0.0 (2026-07-09, commit `13c93a4`) added the writer.
  Commit `4ad985c` (#336, first in 3.8.0) normalizes before quantizing. Older
  output clamps every component of an unnormalized PLY quaternion that falls
  outside `[−1, 1]`. Commit `bb64f9f` (#370, first in 3.11.0) writes alpha
  from the model's linear opacity, clamped to 1, rather than from a logit.
  The stored value is the same for models whose opacity stays in `[0, 1]`.
- gsplat.js wraps rather than clamps: a component of exactly `+1` encodes as
  `0`. Because the other three components must then be 0, the result decodes
  to `−q`, which is the same rotation.
- The two rotation read rules differ by half a step. `(b − 128) / 128` is the
  exact inverse of every writer's `q·128 + 128`, and four of the five readers
  use it. SplatTransform's `b / 255 · 2 − 1` happens to centre truncated
  values better. §6 measures both.
- A file does not record which writer produced it, and the statistics of
  its bytes do not reveal the arithmetic either (§8).

Related formats that are not variants of `.splat`: `.ksplat`
(GaussianSplats3D, with a header and compression modes; backlog) and
`.splatv` (antimatter15's time-varying format, with its own header). Both
use their own extensions, so they never reach a `splat` registration.

## 4. Writers

| Producer | Writes `.splat` | Frame | Evidence |
| --- | --- | --- | --- |
| antimatter15 `convert.py` and viewer | yes | PLY columns copied verbatim | source |
| SplatTransform ≥ 3.0.0 | yes, by `.splat` output extension | bakes to `Transform.PLY` before writing | `write.ts`, `write-splat-streaming.ts` |
| SuperSplat | yes, export type `splat` through SplatTransform 3.11.2 | as SplatTransform | `file-handler.ts`, `package.json` |
| gsplat.js | yes, `SplatData.serialize()`; its PLY import converts Polycam's frame to the PLY frame | PLY frame | `PLYLoader.ts` |
| Spark | yes, `AntiSplatEncoder` | the in-memory data, unchanged | `antisplat.rs` |
| Polycam, Luma, Scaniverse, Postshot | not found | — | Their documentation lists PLY (and SPZ for Scaniverse); a forum post mentions `.splat` from Polycam, but no official export option was found. |

`.splat` is a web-viewer and conversion format. No capture app was found that
writes it. Every reader examined, and SplatTransform's writer, selects the
format by extension: Spark's content sniffing recognises PLY, SPZ, and ZIP,
and says in a comment that `.splat` "require[s] … the appropriate file
extension".

## 5. Coordinate frame

Every writer in §4 stores the same raw columns that a Graphdeco PLY stores.
The antimatter15 converters copy `x, y, z` and `rot_0..rot_3` unchanged.
SplatTransform bakes to `Transform.PLY`, which `math.ts` documents as the
convention shared by "PLY, splat, KSplat, SPZ, and SOG", and its reader
reports `Transform.PLY` with no per-component negation. This matches the
[SOG frame decision](../reference/SOG_FORMAT.md#6-format-decisions).

The `.splat` decoder therefore applies the shared `FlipYZAxes` helper
([ADR 0001](../adr/0001-model-frame-is-rub.md)) to positions and to the
quaternion vector part. There are no rest SH coefficients, so the sign table
is never used. A `.splat` and the PLY it was made from then import to the same
orientation.

As for PLY, the frame is a convention, not a declaration. A file whose writer
did not start from Graphdeco-frame data imports in that writer's frame.
Inferring the frame is a non-goal of the
[model contract](../reference/GAUSSIAN_MODEL_CONTRACT.md).

## 6. Quantization loss

| Attribute | Storage | Loss |
| --- | --- | --- |
| Position | f32 | none |
| Scale | linear f32 | float32 rounding of `exp(log-scale)` only; the model stores linear scale, so decoding needs no conversion |
| DC colour | u8 of `0.5 + SH_C0 · f_dc` | step `1 / (255 · SH_C0) ≈ 0.0139` in DC; values outside `±0.5 / SH_C0 ≈ ±1.7725` are clipped |
| Opacity | u8 of `sigmoid(opacity)` | step `1 / 255`; an alpha byte of 0 decodes to opacity 0 |
| Rotation | u8 × 4 | at most ~1.75° (below) |
| Rest SH | not stored | every coefficient above band 0; the decoded SH degree is always 0 |

**Rotation.** A script encoded 2,000,000 random unit quaternions with each
writer arithmetic, decoded them with each read rule, normalized the result,
and measured the angle to the original:

| Write | Read | Mean | p99 | Max |
| --- | --- | --- | --- | --- |
| truncate | `(b − 128) / 128` | 0.853° | 1.423° | 1.748° |
| truncate | `b / 255 · 2 − 1` | 0.426° | 0.712° | 0.873° |
| round | `(b − 128) / 128` | 0.426° | 0.712° | 0.888° |
| round | `b / 255 · 2 − 1` | 0.853° | 1.423° | 1.751° |

Neither read rule is better for every writer, and the file does not identify
its writer. `(b − 128) / 128` is the reference rule and the exact inverse of
the encoding, so the equivalence tolerance must cover about 1.75°.

**DC clipping.** The committed CC0 subsets were checked with the same
formula. 32.17 % of the Gaussians in `yashica-t4-top8192.ply` and 25.04 % in
`leica-sofort-top8192.ply` have at least one DC channel outside the range
`.splat` can store. Their colour is clamped on conversion. Both subsets also
lose 45 rest SH coefficients per Gaussian. None of their opacities falls
below half an alpha step. The subsets hold the highest-ranked Gaussians of
each capture, so the full captures may differ.

## 7. Routing

- **Sdf selects by extension.** As recorded in the
  [glTF spike](format-spike-gltf-gaussian-splatting-2026-11-19.md#8-registration-and-coexistence),
  `SdfLayer` finds a file format by extension and never calls `CanRead()`.
  The registry lowercases extensions (`fileFormatRegistry.cpp`), so `.SPLAT`
  routes the same way. `.ksplat` and `.splatv` have different extensions and
  are not claimed.
- **No competing claim.** The OpenUSD 26.05 runtimes used by this repository
  register `usd`, `usda`, `usdc`, `usdz`, `mtlx`, and example extensions. The
  Adobe plugins register `fbx`, `gltf`/`glb`, `obj`, `ply`, `sbsar`, `spz`, and
  `stl`. Neither registers `splat`. The bundle can declare `"primary": true`
  like the existing bundles.
- **It cannot claim other bundles' files.** The PLY, SPZ, and SOG bundles
  register `ply`, `spz`, `sog`, and `json`. A `splat` registration is never
  consulted for those extensions, so it cannot change their routing or
  diagnostics.
- **`CanRead()`.** Return true for the `splat` extension when the file exists,
  is non-empty, and its size is a multiple of 32. This needs only a stat, not
  a read. A file that fails the gate is not a `.splat` file in any producer's
  terms.
- **`Read()` checks for renamed files.** A `.splat` file has no magic number,
  but the other formats do. Read the first four bytes, including on a
  metadata-only read, and reject `ply\n`, `NGSP`, `PK\x03\x04`, and `glTF`
  with a diagnostic that names the format the file appears to be. A genuine
  `.splat` file would collide only if its first position component had
  exactly the bit pattern of one of these signatures. The 2-byte gzip
  signature of SPZ v1-v3 is too short to reject on, so a renamed gzip file is
  left to per-record validation: random bytes fail the finite-value and
  positive-scale checks within a few records.

## 8. Fixtures

- **Deterministic.** A generator that implements §2 needs no third-party
  package. It can write `.splat` partners for the equivalence models, both
  truncating and rounding, and the negative fixtures: wrong size, zero
  length, non-finite position, zero or negative scale, all-128 rotation, and
  each renamed signature.
- **Derived from the CC0 corpus.** `yashica-t4-top8192.ply` and
  `leica-sofort-top8192.ply` can be converted with the generator, or with
  SplatTransform 3.11.2 run locally by the maintainer, as was done for the
  SOG corpus. Each file is 262,144 bytes, and its provenance is recorded
  next to the source PLY's.
- **Reference-written.** SplatTransform's `test/fixtures/splat/minimal.splat`
  (MIT, 128 bytes, 4 records, SHA-256
  `43410dad38d82e7e3b8992750ba891641b86f3bb10011d6f0e3419836dcfd38f`) is an
  independent decode check for position, scale, colour, and alpha. Its
  generator comment calls the rotation bytes `128, 128, 128, 255` an
  identity in `x, y, z, w` order. Read in the format's `w, x, y, z` order,
  they are a 180° rotation about Z. The Gaussians are isotropic, so the
  rendering is unaffected, but the file must not be used to check
  orientation.
- **Public samples.** The antimatter15 demo files (`cakewalk/splat-data` on
  Hugging Face) have no recorded licence, and most are derived from the
  Graphdeco and Mip-NeRF 360 scenes, so they cannot be committed. Local
  inspection of `plush.splat` (SHA-256
  `80e0ae670ee980f166d9b00045d33710dd77e65ff004f1886ab442220e2c48fa`,
  281,498 records) agreed with §2: the size is a multiple of 32, every value
  is finite, every scale is positive, and no rotation is all-128. 11.6 % of
  its Gaussians have a colour channel at 0 or 255, which is consistent with
  DC clipping. The mean norm of its decoded rotations (0.99987) matches the
  simulations of both truncating and rounding writers (1.00002 and 1.00001),
  so its writer's arithmetic cannot be identified.

## 9. Hosting bundle

**Recommendation: a new `gaussian-splat` bundle**, with the diagnostic prefix
`GSSPLAT-` and `provides: usd-fileformat:splat`.

- Each existing bundle owns one container family, its manifest fixtures, and
  its diagnostic catalogue
  ([workspace](../architecture/WORKSPACE.md)). Compressed PLY lives in
  `gaussian-ply` because it shares the `.ply` extension and is routed by
  header; `.splat` shares neither.
- The decoder needs only `gaussianCore` (`FlipYZAxes`,
  `NormalizeQuaternion`) and `gaussianUsd`. Nothing in `gaussian-ply` would
  be reused.
- The cost is one more package per release target in the release workflow,
  the aggregate package consumer test, and the bundle CI matrix. There is no
  vendored code, so there are no new third-party notices.

The alternative is a second `SdfFileFormat` type in the `gaussian-ply`
library. That saves a package, but it would put `.splat` diagnostics under
`GSPLY-` and make a PLY package claim a non-PLY extension.

## 10. Proposed scope for FMT-14 and FMT-15

- **In scope.** Full and metadata-only reads of the §2 layout. The
  metadata-only read authors the count (`fileSize / 32`) and SH degree 0 from
  the file size and the four-byte signature check, without reading records.
- **Decoding.** Positions as-is. Linear scales. DC from
  `(c / 255 − 0.5) / SH_C0`. Opacity `a / 255`. Rotation from
  `(b − 128) / 128` through `NormalizeQuaternion`, so an all-128 rotation
  becomes identity with a warning, as the model contract requires. Then
  `FlipYZAxes`.
- **Rejected with specific diagnostics.** A size that is zero or not a
  multiple of 32; a count above the shared 8,000,000 ceiling (256,000,000
  bytes), checked before allocation; a renamed PLY, SPZ v4, SOG, or GLB file
  (§7); a non-finite position, scale, or rotation; and a scale that is zero or
  negative.
- **Equivalence.** Add `.splat` partners to the PLY equivalence models at
  degree 0. Tolerances are half a step for DC and opacity (rounding writer)
  or a full step (truncating writer), and about 1.75° for rotation. Position
  and scale use float32 tolerance. Fixtures with rest SH compare DC only and
  assert that the decoded degree is 0.
- **Package impact (FMT-15).** The new bundle's manifest declares smoke,
  round-trip, and negative fixtures. The aggregate package consumer test and
  the release workflow gain the bundle. The capability matrix, a
  `SPLAT_MAPPING.md` reference, the diagnostics index, and import limits are
  updated with the implementation evidence.

## 11. Not verified in this spike

- No `.splat` file written by SuperSplat, gsplat.js, or Spark was examined.
  Their behaviour was read from source.
- `plush.splat` is the only real file examined, and its writer is unknown.
- SplatTransform version ranges come from the tags that contain each commit,
  not from running the tool. Running npm packages was not permitted in this
  session.
- The commercial-app survey in §4 relied on vendor documentation and search
  results, not on the apps.
