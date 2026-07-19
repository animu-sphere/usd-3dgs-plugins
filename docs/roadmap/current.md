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
- ⬜ Document the `CanRead()` contract: it indicates plausible format
  compatibility (extension and header shape), not complete asset validity.
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

## Shared model contract ⬜

*Goal: document `GaussianCloudData` clearly enough for two independent
decoders to target it consistently. Format-specific encodings (Graphdeco
log-scales and opacity logits, SPZ quantized values) never enter the shared
model.*

- ⬜ Document the contract: coordinate system and axis assumptions, position
  units, scale and opacity representation at the model boundary, quaternion
  component order and normalization, SH degree and coefficient ordering,
  array-length relationships to `gaussianCount`, and finite-value/range
  validation requirements.
- ⬜ Add shared invariant tests and confirm the PLY decoder conforms to the
  documented contract.
- ⬜ Decide which validation and math utilities are shared under `libs/` and
  which stay format-specific.

## SPZ container reader ⬜

- ⬜ Identify the authoritative SPZ specification or reference implementation;
  record supported versions, field encodings, and quantization rules; review
  licenses; and decide whether to implement from the specification, vendor a
  small dependency, or wrap a library. Undocumented binary layouts are not
  guessed.
- ⬜ Implement low-level reading separate from semantic conversion: signature
  detection, version parsing, header/count/size validation with overflow-safe
  buffer math, truncated-input detection, unsupported-version rejection, and
  a metadata-only path. The reader constructs no USD objects.
- ⬜ Add invalid-container fixtures.

## SPZ semantic decoder ⬜

- ⬜ Decode into `GaussianCloudData`: position/scale dequantization, rotation
  decoding and normalization, opacity decoding, SH coefficient decoding with
  degree inference or validation, finite-value and range checks, and shared
  cloud validation — the same output invariants as `GaussianPlyDecoder`.
- ⬜ Introduce the stable `GSPZ-****` diagnostic namespace with a
  machine-readable catalog cross-checked by tests, distinguishing malformed,
  unsupported, and internal failures.
- ⬜ Add unit fixtures for each supported encoding case: minimum valid SPZ,
  multiple records, each supported SH degree, quantization boundary values,
  quaternion normalization, truncated input, invalid counts, unsupported
  versions, and metadata-only reads.

## USD integration ⬜

- ⬜ Register the SPZ file-format plugin (`plugins/gaussian-spz`) and route
  decoded data into `GaussianLayerWriter`. SPZ-specific USD prim construction
  is not permitted; if SPZ turns out to require a parallel authoring path,
  the release architecture is reconsidered before shipping.
- ⬜ Confirm PLY and SPZ author the same stage hierarchy (`/Asset`,
  `/Asset/Splat`), schema, metadata policy, stage metrics, and default-prim
  behavior.
- ⬜ Add Python/OpenUSD smoke tests.

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
