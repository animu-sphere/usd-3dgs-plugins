# `.splat` format scope and decisions

This records the format source and file-layout decisions for the
`gaussian-splat` importer. See [SPLAT_MAPPING.md](SPLAT_MAPPING.md) for the
semantic mapping and the
[FMT-13 spike](../reports/format-spike-splat-2026-11-19.md) for the evidence
gathered before implementation.

## 1. Specification source

`.splat` has no written specification. The MIT-licensed
[antimatter15/splat](https://github.com/antimatter15/splat) viewer defines it:
its `convert.py` is the reference writer and `main.js` the reference reader
(commit `ba182b51b7c2`). SplatTransform, gsplat.js, GaussianSplats3D, and
Spark read the same layout, and SplatTransform, SuperSplat, gsplat.js, and
Spark write it. The layout below is stated in both the reference reader and
writer, so it is documented by an independently implementable reference rather
than inferred from sample files, which meets the
[format selection policy](../design/DESIGN_POLICY.md#4-format-selection). No
upstream code is copied into this repository.

## 2. Record layout

A file is a sequence of 32-byte records, one per Gaussian, with no header,
footer, or padding. All values are little-endian.

| Offset | Type | Field |
| --- | --- | --- |
| 0 | f32 × 3 | position |
| 12 | f32 × 3 | linear scale |
| 24 | u8 × 3 | colour |
| 27 | u8 | alpha |
| 28 | u8 × 4 | rotation, scalar first (`w, x, y, z`) |

The Gaussian count is `fileSize / 32`. The format stores no version, SH
degree, rest coefficients, frame, units, or producer. Record order carries no
meaning; the reference converter sorts by importance and other writers keep
the source order. [SPLAT_MAPPING.md](SPLAT_MAPPING.md) gives the encoding of
each field.

## 3. Scope

- **Supported.** The layout in §2, through full and metadata-only reads.
- **Writer arithmetic.** Writers differ in whether they truncate or round the
  8-bit fields and whether they normalize the quaternion first (spike §3). The
  file does not record which writer made it, so the decoder applies one rule
  to all files and the tests hold it to both arithmetics.
- **Not `.splat`.** `.ksplat` (GaussianSplats3D) and `.splatv` (antimatter15's
  time-varying format) have their own extensions and headers and are not
  claimed by this bundle. `.ksplat` is in the [backlog](../roadmap/backlog.md).

## 4. Routing

- **Extension.** The bundle registers `splat` as the primary file format for
  the `usd` target. Sdf selects a file format by extension, lower-cased, and
  never calls `CanRead()`. No other plugin examined (the OpenUSD 26.08
  runtimes, the Adobe USD file-format plugins) registers `splat`, and
  this repository's other bundles register `ply`, `spz`, `sog`, and `json`,
  which a `splat` registration never sees.
- **`CanRead()`.** True when the extension is `splat` and the file is a
  regular, non-empty file whose size is a multiple of 32. It needs only a
  stat. A true result is a routing decision, not a promise that `Read()`
  succeeds.
- **Renamed files.** `Read()`, including a metadata-only read, reads the first
  four bytes and rejects `ply\n`, `ply\r`, `NGSP` (SPZ version 4),
  `PK\x03\x04` (ZIP, such as a bundled SOG), and `glTF` (GLB) with
  `GSSPLAT-E003`, naming the format. The signature check runs before the size
  check, so a renamed file is named as such whatever its length. A genuine
  `.splat` file collides only if its first x coordinate has exactly one of
  these bit patterns. The two-byte gzip signature of SPZ versions 1-3 is too
  short to reject on; such a file fails the per-record value checks instead.

## 5. Bounds

The count implied by the file size is checked against the shared
8,000,000-Gaussian ceiling ([import limits](IMPORT_LIMITS.md)) before any
record is read, so the largest accepted file is 256,000,000 bytes. The reader
then reads exactly `count × 32` bytes into one buffer, and the decoder
allocates the model arrays through the shared overflow-checked helpers.

## 6. Decisions

Recorded 2026-11-19, when the maintainer accepted the spike's
recommendations:

- **Hosting bundle.** A new `gaussian-splat` bundle with the diagnostic prefix
  `GSSPLAT-`, rather than a second file format in `gaussian-ply`. `.splat`
  shares neither an extension nor a header with PLY, and the bundle needs only
  `gaussianCore` and `gaussianUsd`.
- **Rotation read rule.** `(b − 128) / 128`, then normalization: the exact
  inverse of every writer's `q · 128 + 128`, and the rule of the reference
  reader and three of the four other readers. SplatTransform's
  `b / 255 · 2 − 1` is not used (SPLAT_MAPPING.md §4).
- **Frame.** Graphdeco PLY frame (RDF), converted with the shared
  `FlipYZAxes`, as for PLY and SOG (SPLAT_MAPPING.md §5).
- **Dependencies.** None.

## 7. Fixtures

- `plugins/gaussian-splat/tools/generate_fixtures.py` writes the
  deterministic fixtures in `tests/fixtures/`: hand-checked records, a zero
  rotation, and one negative fixture per file-layout and value diagnostic.
- The same script converts the committed CC0 PLY subsets to
  `tests/corpus/*/*.splat` with the reference writer's arithmetic, keeping the
  source order, and records their provenance.
- `tests/reference/splattransform-minimal.splat` is SplatTransform's MIT test
  file, an independent check that is not packaged.
- `tools/generate_equivalence_fixtures.py` writes `.splat` partners for the
  shared equivalence models ([EQUIVALENCE.md](EQUIVALENCE.md)).
- No real `.splat` capture with a redistribution licence was found.
