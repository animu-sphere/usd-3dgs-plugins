# Current

The active development target is **v0.4.0 — Gaussian Import Foundation**,
defined in the [release plan](release-plan.md): turn the decoder-to-USD seam
proven by PLY and SPZ into a formal, documented, reusable contract before a
third format depends on it. This release adds no end-user file format; it
formalizes the existing
`format reader → semantic decoder → GaussianCloudData → GaussianLayerWriter →
ParticleField3DGaussianSplat` pipeline (design policy §7.4) so SOG (v0.5.0) and
later decoders can be added without format-specific USD authoring, duplicated
validation, inconsistent coordinate handling, or incompatible diagnostics.

v0.1.0, v0.2.0, and v0.3.0 are tagged and published; their completed milestone
detail is recorded in the [delivery history](../reports/delivery-history.md)
and the [release records](../releases/README.md). In the qualified sequences of
the [roadmap README](README.md), v0.4.0 delivers the design-policy §7.4
conversion-layer formalization and precedes format Phase 3, whose first
candidate — SOG M1 (one object) — is the v0.5.0 theme.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

## Carried over from earlier stabilization

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

## 1. Formal decoder contract ✅

*Goal: a revised, normative `GaussianCloudData` output contract that a decoder
targets without reading PLY or SPZ code.*

- ✅ Revise [GAUSSIAN_MODEL_CONTRACT.md](../reference/GAUSSIAN_MODEL_CONTRACT.md)
  to state, normatively: decoded physical positions; strictly positive linear
  scales; normalized scalar-first quaternions; opacity in `[0, 1]`; supported
  SH degrees and canonical coefficient ordering; Gaussian-major array layout;
  required and optional arrays and their length relationships; finite-value
  requirements; empty-cloud behavior; maximum-count and overflow policy; and
  which source metadata may be retained without affecting semantics.
- ✅ State explicitly that format-native representations never enter the shared
  model: PLY log-scales and opacity logits, SPZ quantized planes, and SOG WebP
  pixels, codebook indices, and palette labels are all converted first.

## 2. Shared semantic validation 🚧

*Goal: one implementation of the validation identical for every decoded cloud,
run by each decoder rather than copied per bundle. The existing
`gaussianCore` contract checker is the seed.*

- 🚧 Extract or consolidate under `libs/`: component-array length consistency,
  finite values, strictly positive scales, normalized-or-normalizable
  rotations, opacity range, SH degree/coefficient-count consistency,
  count-overflow and allocation checks, and extent-computation preconditions.
  Landed so far: the shared gate now enforces the SH degree ceiling
  (`kMaxShDegree`, shared with the SPZ decoder; PLY rejects with
  `GSPLY-E017`) and quaternion normalization at the contract tolerance.
  Still open: shared overflow-checked size/allocation helpers for readers.
- ✅ Keep container-structure validation format-specific — readers remain
  responsible for their own containers; the contract's §3 *Maximum count and
  overflow* section records the split.

## 3. Coordinate-system ADR ✅

*Goal: a normative decision, before SOG decoding lands, for the canonical
coordinate frame and how formats convert into it.*

- ✅ [ADR 0001](../adr/0001-model-frame-is-rub.md) (accepted 2026-07-22):
  the canonical frame of `GaussianCloudData` is **RUB (Y-up)**, matching the
  authored `upAxis = "Y"` — the pre-1.0 authored-USD correction taken in
  v0.4.0 rather than deferred to a major bump. PLY (and later SOG) convert
  RDF→RUB through the shared `FlipYZAxes` helper in `gaussianCore`; SPZ is
  natively RUB and converts nothing. The quaternion/SH sign derivations, the
  alternatives considered (corrective `xformOp`, status quo), and the
  migration consequences for v0.1.0-v0.3.0 output are recorded in the ADR;
  goldens and equivalence fixtures were regenerated in the same change.

## 4. Decoder test kit ⬜

*Goal: reusable helpers to test a decoder against the shared contract without
authoring a USD stage.*

- ⬜ Provide canonical one- and multi-Gaussian expected models, quaternion
  comparison with sign equivalence, SH layout/degree checks, tolerance-aware
  comparison, invalid shared-model cases, deterministic extent comparison, and
  fixtures distinguishing point, coefficient, and channel ordering.
- ⬜ Demonstrate the kit with a minimal mock decoder that targets the shared
  contract using no PLY or SPZ code.

## 5. Import statistics seam ⬜

*Goal: a common optional result structure so per-format instrumentation cannot
diverge. The plugin API need not expose all of it in v0.4.0.*

- ⬜ Define fields for source format and version, Gaussian count, SH degree,
  source byte size, decoded semantic byte size, bounding box, and reader /
  semantic-decode / USD-authoring times.

## 6. Public/internal API boundary ⬜

*Goal: classify headers and targets deliberately, with no ABI promise before
v1.0.0.*

- ⬜ Separate format-plugin implementation details, workspace-shared internal
  libraries, deliberately reusable contributor APIs, and any stable public
  APIs. A header must not read as public merely because it is installed.

## 7. Build, package, and CI scaling ⬜

*Goal: adding a third bundle (`gaussian-sog`) requires declarative
configuration, not copied release logic.*

- ⬜ Verify root plain-CMake composition, standalone library/plugin builds,
  cross-plugin-independent package installation, release-matrix generation from
  one source, per-bundle diagnostic-catalog validation, and early package
  testing for a new bundle skeleton. Record the aggregate-artifact policy even
  if deferred (see [backlog](backlog.md#packaging-and-release)).

## 8. Documentation synchronization 🚧

*Goal: bring the roadmap, release, and reference docs onto the adopted
v0.4.0/v0.5.0 direction and remove drift left by the v0.3.0 release.*

- ✅ README release status: v0.3.0 published, v0.4.0 the current target, v0.5.0
  named as SOG import.
- ✅ Current development target: this file reframed from the completed v0.3.0
  breakdown to the v0.4.0 workstreams; the v0.3.0 detail moved to the
  [delivery history](../reports/delivery-history.md).
- ✅ Release plan: sequence table, v0.3.0 marked shipped, and the v0.4.0 and
  v0.5.0 sections rewritten to Gaussian Import Foundation and SOG v2 import.
- ✅ Backlog priority and milestone ladders: M5/SPZ recorded as shipped, SOG M1
  promoted to the v0.5.0 theme, glTF/GLB reconsidered after SOG.
- ✅ v0.3.0 release-record status finalized to published, and its
  forward-looking version pins corrected (SPZ v4 is no longer a v0.5.0 item).
- ✅ Documentation index and the SPZ scope note corrected so no doc still calls
  v0.3.0 the current target or schedules SPZ v4 for v0.5.0.
- ⬜ Contributor guide: "Adding a format decoder", generalizing
  [CONTRIBUTING.md](../../CONTRIBUTING.md) from the PLY-specific path to the
  shared decoder contract, the reader/decoder/diagnostics split, and the shared
  writer.

## 9. SOG skeleton and v0.5.0 plan ⬜

*Goal: land the `gaussian-sog` bundle skeleton and an approved fixture plan so
v0.5.0 begins against proven CI and packaging, without forcing unresolved
shared-contract decisions into v0.4.0.*

- ⬜ Create the `gaussian-sog` bundle skeleton exercising the scaled CI and
  packaging path from item 7.
- ⬜ Record the SOG dependency decisions (ZIP reading, lossless WebP) with
  fixed source revisions and license review before production decoding lands.
- ⬜ Approve the SOG implementation and fixture plan (bundled `.sog` and
  unbundled `meta.json`, the `GSSOG-****` catalog, cross-format equivalence)
  per the [release plan](release-plan.md#v050--sog-v2-one-object-import).

## Completion criteria

v0.4.0 is complete when:

1. PLY and SPZ still produce equivalent authored structure through one writer.
2. Both decoders pass the same shared-model validation tests.
3. Coordinate conversion and stage-axis policy are normative and consistent.
4. A minimal mock decoder can target the shared contract without PLY or SPZ code.
5. Adding a bundle does not require duplicated release-matrix logic.
6. Documentation clearly defines where parsing ends and shared semantics begin.
7. Existing v0.2.0/v0.3.0 performance and correctness baselines show no material
   regression.
