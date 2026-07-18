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
| v0.1.0 | Initial PLY vertical slice | M0-M4 | ✅ tagged — [record](../releases/v0.1.0.md); draft publication pending |
| v0.2.0 | Production-ready Graphdeco PLY import | Phase 1 stabilization | ⬜ current development target |
| v0.3.0 | SPZ import | M5, Phase 2 | ⬜ |
| v0.4.0 | Common Gaussian conversion layer | design policy §7.4 | ⬜ |
| v0.5.0 | Expanded format compatibility | Phase 3, SOG M1 | ⬜ |
| v0.6.0 | Import tooling and diagnostics | — | ⬜ |
| v0.7.0 | Performance and large-asset readiness | Phase 4 | ⬜ |
| v0.8.0 | USD packaging and asset composition | — | ⬜ |
| v0.9.0 | Rendering integration preview — delivered by [hydra-merlin](https://github.com/animu-sphere/hydra-merlin) | — | ⬜ |
| v1.0.0 | Stable import API and file-format support | — | ⬜ |

## v0.2.0 — production-ready Graphdeco PLY import

**Complete and stabilize PLY support before adding another format.**

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

**Validate that the architecture supports a compressed format without
duplicating the PLY-to-USD implementation.**

This is M5 in the [milestone ladder](backlog.md#milestone-ladder): SPZ
detection and version validation, dequantization of position, scale, rotation,
opacity, and SH data, decoding into `GaussianCloudData`, and reuse of the same
USD authoring path as PLY. SPZ support must not introduce a separate USD
authoring implementation.

Done when: supported SPZ files load through the plugin, PLY and SPZ produce
structurally consistent stages, quantization behavior and limits are
documented, and tolerance-aware equivalence fixtures cover valid and invalid
inputs.

Excluded: SPZ export, streaming decompression, GPU decoding, lossless
round-trip guarantees.

## v0.4.0 — common Gaussian conversion layer

**Separate file-format decoding from USD scene generation as a formal,
documented contract.**

The `parser -> GaussianCloudData -> GaussianLayerWriter` pipeline already
exists; this release formalizes it: shared validation, coordinate-conversion,
SH-layout, and diagnostics utilities; the `gaussianUsd` extraction if the SPZ
work triggered it (design policy §7.4); cleaner public/internal API
separation; shared import statistics; and parsing and authoring benchmarks.

Done when: PLY and SPZ share one authoring path, format-specific logic is
isolated from prim construction, shared conversion behavior is unit-tested,
adding a decoder does not require rewriting the scene-graph layer, and
internal APIs are documented for contributors.

## v0.5.0 — expanded format compatibility

Phase 3 candidates, accepted individually: SOG (SOG M1, one object), glTF/GLB
Gaussian extensions (only after the required ADR), `.splat`, `.ksplat`,
SuperSplat-compatible PLY variants, and other documented PLY dialects.
Priority follows specification availability, ecosystem adoption, test-asset
availability, licensing compatibility, maintenance cost, and clean mapping
into the common Gaussian model.

Done when: each stable format has documented version-level coverage, fixtures,
and invalid-input tests, and the generated stage structure remains
format-independent. Undocumented binary layouts are not guessed, and ambiguous
or unsupported variants fail explicitly.

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

The current development target is **v0.2.0**, entered once the v0.1
carry-over in [current.md](current.md) closes. It stays focused on making
Graphdeco PLY support complete, robust, tested, and documented; SPZ and other
formats begin only after the v0.2.0 completion criteria are met.
