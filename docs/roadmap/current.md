# Current

The active development target is **v0.3.0 — SPZ import**, defined in the
[release plan](release-plan.md): stabilize the post-v0.2.0 repository state,
then prove that the common Gaussian import architecture supports a compressed
second format through the existing
`GaussianCloudData -> GaussianLayerWriter` pipeline. v0.1.0 and v0.2.0 are
tagged and published; their completed milestone detail is recorded in the
[delivery history](../reports/delivery-history.md) and the
[release records](../releases/README.md). In the qualified sequences of the
[roadmap README](README.md), v0.3.0 delivers delivery milestone M5
(`gaussian-spz`) and opens format Phase 2.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

## Carried over from v0.1/v0.2 stabilization

Release-engineering items that remain live across releases:

- ⬜ Investigate the macOS across-run package digest difference (suspected
  Mach-O `LC_UUID`/timestamp analog). Windows and Linux `tar.zst` archives are
  byte-identical across hosted runs since v0.2.0; see
  [releases/v0.2.0.md](../releases/v0.2.0.md) and
  [dogfooding report 02](../reports/ost/02-2026-07-19-package-provenance-and-reproducibility.md).
- ⛔ Make package-origin L5 execute rather than skip. OST 0.18.0 packages the
  roundtrip PLY fixture but not its adjacent `.golden.usda`, and the bundle
  manifest has no golden declaration. This is an upstream packaging/test seam;
  see [dogfooding report 01](../reports/ost/01-2026-07-18-v0.18.0-bootstrap.md).
- ⬜ Decide whether Windows remains capped at OST L4 or can run the same L5
  golden gate as macOS/Linux. Local Windows L5 passes; the cap is inherited
  from the reference workspace's hosted multiline-USDA line-ending finding.
- ⬜ Add a lightweight link/language check to CI so public Markdown remains
  English and local links resolve.

## Post-v0.2.0 cleanup 🚧

*Goal: bring the repository, documentation, and release state into a
consistent post-v0.2.0 condition before SPZ work begins.*

- ✅ v0.1.0 and v0.2.0 drafts reviewed and published 2026-07-19; release
  records, roadmap, README, and changelog updated to the published state, and
  v0.3.0 set as the active target.
- ✅ Documented the `CanRead()` contract as design policy §7.6: it indicates
  plausible format compatibility (extension and header shape), not complete
  asset validity, and a too-narrow signature silently disowns readable assets.
- ⬜ Triage defects found in real v0.2.0 usage into GitHub issues (suggested
  labels: `v0.3.0`, `spz`, `bug`, `documentation`, `testing`, `release`).
  Every accepted fix ships with a regression fixture or test where practical,
  a stable diagnostic update when user-visible failure behavior changes, a
  changelog entry, and confirmation that valid v0.2.0 behavior stays
  compatible. Diagnostic codes shipped in v0.2.0 are never renumbered or
  reused.
- ⬜ Improve test reporting where failures are currently too coarse.
- ⬜ Draft the v0.3.0 release-record skeleton early in development (the record
  itself is created only once the tag exists, per the
  [release-record policy](../releases/README.md)).

## Shared model contract 🚧

*Goal: document `GaussianCloudData` clearly enough for two independent
decoders to target it consistently. Format-specific encodings (Graphdeco
log-scales and opacity logits, SPZ quantized values) never enter the shared
model.*

- ✅ Documented the contract in
  [GAUSSIAN_MODEL_CONTRACT.md](../reference/GAUSSIAN_MODEL_CONTRACT.md):
  coordinate system and axis assumptions, position units, scale and opacity
  representation at the model boundary, quaternion component order and
  normalization, SH degree and coefficient ordering, array-length
  relationships to `gaussianCount`, and finite-value/range validation.
- ✅ Extracted `libs/gaussian-usd` (design policy §7.4) so PLY and SPZ author
  through one `GaussianLayerWriter`. Authoring diagnostic codes are injected
  by the calling bundle, so the `GSPLY-E1xx` spellings released in v0.2.0 are
  unchanged and SPZ can pass `GSPZ-****` through the same path.
- ✅ Added contract-conformance tests: `gaussianUsd_writer_unit` pins each
  authoring failure to its injected code, and the PLY decoder suite asserts the
  documented invariants (linear positive scales, opacity in `[0,1]`,
  normalized quaternions, Gaussian-major SH, array lengths) across every valid
  fixture. The checker itself lives in `gaussianCore`, so `gaussian-spz` will
  run the same code against its own fixtures rather than a copy of it.
- ⬜ Decide which validation and math utilities are shared under `libs/` and
  which stay format-specific.

## SPZ container reader ✅

- ✅ Identified the specification and recorded scope decisions in
  [SPZ_FORMAT.md](../reference/SPZ_FORMAT.md): Niantic's MIT-licensed
  documented format; **implement from the specification** (a vendored decoder
  cannot express the required malformed/unsupported/internal distinction), and
  **support versions 1-3 in v0.3.0** with v4 deferred to v0.5.0 and rejected
  by a specific unsupported-version diagnostic.
- ✅ Implemented low-level reading separate from semantic conversion
  (`plugins/gaussian-spz/src/io/SpzReader.*`): signature detection for both
  container generations, gzip member framing parsed in-repo (vendored miniz
  supplies raw DEFLATE and CRC32 only), header validation, overflow-safe
  size math with a DEFLATE-expansion plausibility bound, truncation /
  corruption / trailing-data detection, unsupported-version rejection, and a
  metadata-only header path. The reader constructs no USD objects and emits
  the `GSPZ-E0**` container series of the stable diagnostic catalog.
  Implementing from the specification surfaced three errors in the recorded
  container facts (attribute ordering, v1 float16 positions, per-version
  rotation encodings), corrected in [SPZ_FORMAT.md](../reference/SPZ_FORMAT.md)
  §4 first.
- ✅ Decided the SPZ `CanRead()` strategy
  ([SPZ_FORMAT.md](../reference/SPZ_FORMAT.md) §6): plaintext v4 magic
  claimed for the specific unsupported-version diagnostic; gzip inputs
  identified by a bounded partial decompression of the magic only, with
  header-field validation deliberately left to `Read()`.
- ✅ Added container fixtures: 9 valid (versions 1-3, multi-point SH sizing,
  spec-max degree 4, extension records, optional gzip header fields with a
  verified FHCRC, an FNAME long enough to force the bounded `CanRead()`
  retry) and 20 invalid, generated
  deterministically by `plugins/gaussian-spz/tools/generate_fixtures.py`, with
  `tests/test_gaussian_spz_reader.cpp` pinning every fixture to its exact
  diagnostic code.

## SPZ semantic decoder ✅

- ✅ Decode into `GaussianCloudData` (`plugins/gaussian-spz/src/io/GaussianSpzDecoder.*`):
  position dequantization (v1 float16, v2/v3 24-bit fixed point), 8-bit
  log-scale and opacity decoding, per-version rotation decoding
  (first-three and smallest-three) with normalization, DC and rest SH
  dequantization with the channel-inner-to-Gaussian-major transpose, the
  RUB→RDF reference-frame conversion, finite-value checks, and shared cloud
  validation — the same output invariants as `GaussianPlyDecoder`. The exact
  mapping and the reference constants are pinned in
  [SPZ_MAPPING.md](../reference/SPZ_MAPPING.md).
- ✅ Extended the stable `GSPZ-****` catalog with the decoding, authoring, and
  entry-point codes (`E011`-`E013`, `E101`-`E104`, `E201`, `W001`-`W002`);
  `diagnostics.json` is cross-checked against the source constants in both
  directions by the Python smoke test. Malformed / unsupported / internal
  failures stay distinct: SH degree 4 is unsupported (`E011`), not malformed.
- ✅ Added decoder fixtures generated deterministically by
  `tools/generate_fixtures.py` (known Gaussians encoded through the reference
  quantization formulas) and `tests/test_gaussian_spz_decoder.cpp`, pinning the
  RUB→RDF conversion, the per-coefficient SH flip signs, both rotation
  encodings, unsupported degree 4, and non-finite float16 positions. The
  highest-risk paths (rotation reconstruction, SH ordering) are checked as an
  encode→decode inverse, not against re-derived formulas.

## USD integration ✅

- ✅ The SPZ file-format plugin routes decoded data through the shared
  `GaussianLayerWriter` under this bundle's own stable authoring codes; no
  SPZ-specific USD prim construction exists. `Read()` and metadata-only reads
  both author, and the read-only `WriteToFile` path fails with `GSPZ-E201`.
- ✅ PLY and SPZ author the same stage hierarchy (`/Asset`, `/Asset/Splat`),
  schema, metadata policy, stage metrics, and default-prim behavior by
  construction — one writer, differing only in the `sourceFormat` string.
- ✅ Added `tests/test_gaussian_spz_plugin.py`: opens SPZ stages through
  OpenUSD, asserts the shared stage contract and the known decoded values
  (including the axis flip and SH layout seen through USD), the metadata-only
  path, and every negative fixture's exact diagnostic code.

## Equivalence and real assets ⬜

- ⬜ Create matching or derived PLY/SPZ fixture pairs and compare count, SH
  degree, positions, scales, rotations, opacities, and DC and higher SH
  coefficients with documented quantization-aware tolerances, plus the
  authored hierarchy, schema, and metadata.
- ⬜ Validate at least one legally redistributable SPZ asset with recorded
  provenance: source, encoder and version, conversion command, license,
  Gaussian count, SH degree, checksum, and expected import result.
- ⬜ Record design-policy §12.1 baselines for SPZ (`CanRead`, metadata-only
  read, full decode, stage open, flatten to USDC, peak memory), compared
  against PLY where equivalent assets exist. The v0.3.0 performance bar is
  avoiding architectural regressions (full-buffer copies, duplicate decoded
  representations, repeated dequantization); streaming and GPU decoding stay
  out of scope.

## Release hardening ⬜

- ⬜ The same gate v0.2.0 passed, now including the SPZ bundle: cross-platform
  CI, packaging and SBOM, release guard, documentation review, dry run, tag,
  draft release, and human publication review.
- ⬜ Update the capability matrix, compatibility documents, and
  build/install/usage guides to cover SPZ, including documented quantization
  behavior, precision limits, and supported SPZ versions.
