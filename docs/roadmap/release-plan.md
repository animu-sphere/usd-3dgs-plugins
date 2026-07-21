# Release plan

The versioned release sequence from the tagged v0.1.0 through v1.0.0. It orders
the design-policy phases and milestones
([DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §4, §18) into tagged releases
with one primary theme each. Task-level status stays in [current.md](current.md)
and [backlog.md](backlog.md); tagged behavior gets an immutable record under
[releases/](../releases/).

Principles:

- Stabilize one format before expanding to additional formats.
- Keep file parsing separate from the USD representation layer.
- Establish the common internal Gaussian data model before supporting many
  input formats.
- Treat rendering support as a separate concern from file-format support.
- Define clear completion criteria for every release.
- Prefer small, testable releases over broad but incomplete feature sets.

New formats are not added until the existing implementation is sufficiently
tested and documented.

Legend: ✅ done · 🚧 in progress · ⬜ not started

## Sequence

| Version | Primary theme | Sequence mapping | Status |
| --- | --- | --- | --- |
| v0.1.0 | Initial PLY vertical slice | M0-M4 | ✅ tagged & published — [record](../releases/v0.1.0.md) |
| v0.2.0 | Production-ready Graphdeco PLY import | Phase 1 stabilization | ✅ tagged & published — [record](../releases/v0.2.0.md) |
| v0.3.0 | SPZ import | M5, Phase 2 | ✅ tagged & published — [record](../releases/v0.3.0.md) |
| v0.4.0 | Gaussian Import Foundation | design policy §7.4 | 🚧 current target |
| v0.5.0 | SOG v2 one-object import | Phase 3, SOG M1 | ⬜ |
| v0.6.0 | Import tooling and diagnostics | — | ⬜ |
| v0.7.0 | Performance and large-asset readiness | Phase 4 | ⬜ |
| v0.8.0 | USD packaging and asset composition | — | ⬜ |
| v0.9.0 | Rendering integration preview — delivered by [hydra-merlin](https://github.com/animu-sphere/hydra-merlin) | — | ⬜ |
| v1.0.0 | Stable import API and file-format support | — | ⬜ |

## v0.2.0 — production-ready Graphdeco PLY import

**Complete and stabilize PLY support before adding another format.**

Shipped: tagged and published 2026-07-19 — see the
[release record](../releases/v0.2.0.md).

v0.1.0 already ships name-based property resolution, ASCII and binary
little-endian input, unknown-property tolerance, and required-property
validation. v0.2.0 completes the remaining Phase 1 stabilization work:

- Real-dataset baselines: the design-policy §12.1 metrics on at least one
  provenance-recorded Graphdeco asset ([current.md](current.md), real-asset
  confidence).
- Documented PLY dialect compatibility for Graphdeco reference exports and
  SuperSplat-exported PLY ([backlog](backlog.md#ply-compatibility)).
- Degree-2/3 SH fixtures, multi-Gaussian fixtures, and additional malformed
  cases.
- Stable diagnostic identifiers and a machine-readable diagnostic catalog.
- Metadata-only reads (`Read(metadataOnly=true)`) per design policy §12.3.
- First file-format arguments — candidates are `shDegree`, `opacityThreshold`,
  and `scaleMultiplier` — included only with clear behavior and automated
  tests.

Done when: a Graphdeco Gaussian PLY loads into the stable USD stage safely and
reproducibly, independent of property ordering and of ASCII versus binary
encoding; supported and unsupported behavior is documented; invalid input
fails with actionable diagnostics; and at least one representative real-world
asset has been manually validated.

Excluded: everything sequenced from v0.3.0 onward — SPZ, SOG, `.splat`,
`.ksplat`, export, a conversion CLI, rendering, animation, LOD, streaming,
and editing workflows.

## v0.3.0 — SPZ import

**Stabilize the post-v0.2.0 repository state, then prove that the common
Gaussian import architecture supports a compressed second format without
duplicating the PLY-to-USD implementation.**

Shipped: tagged and published 2026-07-20 — see the
[release record](../releases/v0.3.0.md). This was M5 in the
[milestone ladder](backlog.md#milestone-ladder), delivered in two workstreams:

1. **Post-v0.2.0 stabilization** — resolve documentation and release-state
   inconsistencies left behind by v0.2.0, fix defects found in real usage,
   document the `CanRead()` contract (plausible format compatibility, not
   complete asset validity), improve too-coarse test reporting, and carry
   the open release-engineering items forward. Fixes stay focused: PLY
   support does not expand into undocumented or ambiguous dialects without
   fixtures and an explicit compatibility decision.
2. **SPZ import** — production-quality read-only support: reliable
   detection, container-version and structural validation, dequantization of
   position, scale, rotation, opacity, and SH data, decoding into
   `GaussianCloudData`, and reuse of `GaussianLayerWriter`.

Architectural rules:

- PLY and SPZ converge at `GaussianCloudData`; SPZ-specific USD prim
  construction is not permitted. If SPZ turns out to require a parallel USD
  authoring path, the release architecture is reconsidered before shipping.
- Format-specific encodings stay out of the shared model: SPZ quantized
  values are dequantized before entering it, exactly as Graphdeco log-scales
  and opacity logits are converted today. The `GaussianCloudData` contract
  is documented clearly enough for two independent decoders to target it
  consistently.
- SPZ parsing and dequantization live in a dedicated format-specific module
  (`plugins/gaussian-spz`), mirroring the PLY reader/decoder/diagnostics
  split; USD prim creation, layer authoring, renderer-specific behavior, and
  PLY compatibility logic stay outside the decoder.
- SPZ diagnostics get their own stable `GSPZ-****` namespace and
  machine-readable catalog, following the `GSPLY-****` conventions.

Done when: supported SPZ files open through the plugin; PLY and SPZ share the
same `GaussianCloudData -> GaussianLayerWriter` path and equivalent assets
produce structurally consistent stages; quantization behavior, precision
limits, and supported SPZ versions are documented and covered by
tolerance-aware equivalence fixtures; unsupported versions and malformed
input fail with stable actionable diagnostics; and at least one
provenance-recorded real SPZ asset is validated manually and automatically
with design-policy §12.1 baselines recorded.

Excluded: SPZ export, PLY export, lossless cross-format round-tripping, SOG,
`.splat`, `.ksplat`, compressed SuperSplat PLY (unless formally specified and
separately accepted), glTF/GLB Gaussian extensions, renderer and Hydra work,
editing, animation, LOD, network streaming, GPU-native decoding, a general
conversion CLI (small internal validation or fixture tools excepted), and
public decoder-API guarantees beyond what the plugin needs.

## v0.4.0 — Gaussian Import Foundation

**Turn the decoder-to-USD seam proven by PLY and SPZ into a formal, documented,
reusable contract before a third format depends on it.**

v0.3.0 showed that two substantially different encodings converge at
`format reader → semantic decoder → GaussianCloudData → GaussianLayerWriter →
ParticleField3DGaussianSplat`. This release does not add an end-user format; it
formalizes that seam (design policy §7.4) so SOG and later decoders can be
implemented without format-specific USD authoring, duplicated validation,
inconsistent coordinate handling, or incompatible diagnostics. The task
breakdown is in [current.md](current.md). Scope:

- a revised, normative `GaussianCloudData` output contract
  ([GAUSSIAN_MODEL_CONTRACT.md](../reference/GAUSSIAN_MODEL_CONTRACT.md)) —
  decoded physical values only; format-native representations (PLY log-scales
  and opacity logits, SPZ quantized planes, SOG pixels and codebook indices)
  never enter it;
- shared semantic validation consolidated into `libs/` and run by every
  decoder rather than a per-bundle copy;
- a normative coordinate-system ADR deciding the canonical frame, how formats
  with authoritative axis definitions convert into it, and whether the current
  RDF-model / authored `upAxis = "Y"` mismatch is corrected in this pre-1.0
  window;
- a decoder test kit letting a decoder be tested against the shared contract
  without authoring a USD stage;
- a shared import-statistics seam so per-format instrumentation cannot diverge;
- a documented public/internal header boundary (no ABI promise before v1.0.0);
- build, package, and CI configuration that scales to a third bundle by
  declaration rather than copied release logic;
- documentation synchronization and a contributor guide for adding a decoder.

Done when: PLY and SPZ still produce equivalent authored structure through one
writer and pass the same shared-model validation; coordinate and stage-axis
policy are normative and consistent; a minimal mock decoder can target the
shared contract without PLY or SPZ code; adding a bundle needs no duplicated
release-matrix logic; and the v0.2.0/v0.3.0 performance and correctness
baselines show no material regression.

Excluded: SOG file import and its ZIP/WebP dependencies, SPZ v4, `.splat`,
`.ksplat`, compressed PLY, glTF/GLB, export, a conversion CLI, streaming/LOD,
renderer work, and stable external ABI guarantees.

## v0.5.0 — SOG v2 one-object import

**Import one complete SOG v2 Gaussian object into the same standard USD
representation PLY and SPZ produce, exercising the v0.4.0 contract with a third
format.**

SOG was chosen ahead of `.splat`, `.ksplat`, compressed PLY, and glTF/GLB
Gaussian extensions for its published versioned specification, clean mapping
into the Gaussian semantic model, active PlayCanvas/SuperSplat use,
deterministic test-asset generation through SplatTransform, and a natural path
toward later spatial chunks and streamed LOD. Read-only, one object, two
layouts converging on one reader and decoder:

- bundled `.sog` (a ZIP of `meta.json` plus lossless-WebP property images);
- unbundled `meta.json` with resolver-backed companion images.

The SOG reader and `GaussianSogDecoder` decode positions (`means_l`/`means_u`),
smallest-three quaternions, exponential scale codebooks, SH0/base color and
opacity, and optional higher-order SH palettes into `GaussianCloudData`,
convert the SOG source frame through the v0.4.0 canonical-frame policy, and
author through the unchanged `GaussianLayerWriter`. SOG gets its own stable
`GSSOG-****` diagnostic namespace and machine-readable catalog. New production
dependencies for ZIP reading and lossless WebP decoding are pinned to fixed
source revisions with recorded license and dependency decisions.

Done when: both layouts open through the plugin on the same semantic path;
position, scale, rotation, opacity, SH0, and optional higher-order SH decode
correctly; cross-format fixtures demonstrate coordinate and SH consistency
against PLY/SPZ at tolerances derived from the SOG equations; failures use
stable actionable `GSSOG-****` diagnostics; a provenance-recorded real SOG
asset is validated automatically and manually with recorded performance
baselines; and Windows, macOS, and Linux source/package cells pass.

Excluded: Streamed SOG and `lod-meta.json`, multiple chunks or LOD levels,
deferred/partial loading, network resource loading, lossy property images, SOG
export or recompression, GPU-native decode, generic ZIP/image APIs, glTF/GLB,
`.splat`, `.ksplat`, compressed PLY, and SPZ v4. Streamed SOG and its
composition model are SOG M2-M4, scheduled only after the one-object decoder is
stable and measured.

## v0.6.0 — import tooling and diagnostics

An optional command-line inspection tool that shares the plugin's parsing and
validation code: format identification, attribute/Gaussian-count/SH-degree/
bounding-box summaries, a validation-only mode, import timing statistics,
CI-suitable diagnostic output, and an optional USD conversion command. The
CLI shape is provisional until this release:

```text
usd3dgs inspect asset.ply
usd3dgs validate asset.spz
usd3dgs convert asset.ply output.usda
```

Done when: users can inspect and validate files without launching a viewer,
diagnostics are actionable in automated pipelines, and the CLI behavior is
documented and tested.

## v0.7.0 — performance and large-asset readiness

Predictable loading behavior for large production assets, building on the
v0.2.0 baselines: a benchmark harness separating file read, semantic decode,
USD authoring, and total stage-open time; memory measurements; large
Gaussian-count stress tests; performance regression tracking; a cancellation
or interruption strategy where supported; and the lazy-loading investigation
([backlog](backlog.md#performance-and-loading)). The copy-reduction ladder is
already complete and recorded in the
[delivery history](../reports/delivery-history.md).

Done when: representative large assets load within documented resource
expectations, regressions are measurable in a repeatable suite, and known
scaling limits are documented.

Excluded: full network streaming, GPU-native decoding, renderer-specific
buffer management, final LOD.

## v0.8.0 — USD packaging and asset composition

Integration with real USD asset pipelines: asset metadata conventions,
referencing and payload behavior, packaging and composition examples,
relative asset path and resolver compatibility testing, instanceability
investigation, a clear source-file metadata policy, and stable default-prim
and child-prim naming conventions.

Done when: Gaussian assets participate in common reference and payload
workflows, packaging behavior is documented, resolver-dependent behavior is
tested where practical, and the scene-structure conventions are considered
stable.

## v0.9.0 — rendering integration preview (delivered by hydra-merlin)

The rendering side of this milestone is owned by the sibling project
[hydra-merlin](https://github.com/animu-sphere/hydra-merlin), a Hydra
integration for `ParticleField3DGaussianSplat`. This repository stays
renderer-neutral: a bundled Hydra renderer remains a
[non-goal](backlog.md#non-goals), and rendering work must not compromise
parser correctness or stage portability.

Repository-side scope
([backlog](backlog.md#rendering-and-ecosystem-integration)):

- a renderer capability note tracking Hydra implementations of the schema,
  starting with hydra-merlin;
- documentation keeping import support and rendering support separate claims;
- examples demonstrating downstream inspection without making a renderer a
  core dependency.

File-format compatibility and rendering compatibility remain separate claims:
opening a Gaussian asset as a USD stage does not imply that hdStorm or any
other renderer can display it correctly.

Done when: at least one documented rendering path exists for preview or
experimentation (hydra-merlin), unsupported renderer behavior is clearly
communicated, and the integration is explicitly labeled experimental unless
production quality is reached.

## v1.0.0 — stable import API and file-format support

Declares the core import architecture and the stable formats
production-ready: reliable Graphdeco PLY and SPZ import, the documented
common Gaussian data model, a stable prim hierarchy, stable plugin
configuration behavior, automated regression fixtures, clear compatibility
documentation, actionable diagnostics, documented performance
characteristics, and USD pipeline integration guidance.

Stability commitments from v1.0.0:

- breaking prim-path or file-format-argument changes require a major version;
- stable supported format variants are explicitly listed, and unsupported
  variants fail clearly;
- public APIs and extension points are documented;
- release notes include migration guidance when behavior changes.

Not required for v1.0.0: universal 3DGS format support, Gaussian editing,
animation, network streaming, production Hydra rendering, GPU decoding, or
lossless cross-format round-tripping. v1.0.0 is a stable import and USD
representation foundation, not the completion of every Gaussian Splatting
workflow.

## Post-1.0 directions

Scheduled only after the stable import foundation is established:

- export and round-tripping with explicit loss reporting
  ([backlog](backlog.md#research));
- a dedicated render delegate, scene-index integration, selection/picking,
  and quality modes, owned by
  [hydra-merlin](https://github.com/animu-sphere/hydra-merlin);
- streaming, spatial chunking, LOD, and payload partitioning (SOG M2-M4);
- time-varying and animated Gaussian attributes;
- editing and authoring workflows.

## Versioning policy

The project follows semantic versioning:

- **Patch** — bug fixes, documentation corrections, behavior-preserving
  performance improvements, support for additional valid files within an
  already documented variant, better diagnostics.
- **Minor** — new input formats, new optional file-format arguments, new
  tooling, new non-breaking metadata, experimental rendering features, new
  extension APIs.
- **Major** — breaking prim-hierarchy, public-API, or argument changes;
  incompatible changes to authored USD data; removal of a previously stable
  format variant; any change that requires users to migrate workflows.

Every release passes the [release gate](../releases/README.md#release-gate).
In addition, each release has one clearly stated primary theme,
repository-owned fixtures for new parsers, documented known limitations, and
a reviewed third-party dependency and license list.

## Immediate focus

The current development target is **v0.4.0 — Gaussian Import Foundation**,
entered with the v0.3.0 tag and publication on 2026-07-20. Work starts with the
coordinate-system ADR and the revised shared-model contract before any SOG
decoding lands; the task breakdown is in [current.md](current.md). v0.5.0 SOG
import begins only after the v0.4.0 completion criteria are met — experimental
SOG work may start during v0.4.0, but production SOG code must not force
unresolved shared-contract decisions into that release.
