# Backlog

This is ordered but unscheduled work. The active release milestone is in
[current.md](current.md); completed work is in the
[delivery history](../reports/delivery-history.md).

Legend: 🚧 in progress · ⬜ not started

## Priority ladder

The [design policy](../design/DESIGN_POLICY.md) fixes the standing order of
investment after release stabilization:

| Priority | Work | Status |
| --- | --- | --- |
| P0 | Benchmark real datasets; establish load-time, memory, and output-size baselines | ✅ shipped in v0.2.0 — [PERFORMANCE_BASELINES.md](../reference/PERFORMANCE_BASELINES.md) |
| P1 | Metadata-only reads | ✅ shipped in v0.2.0 |
| P2 | Documented PLY dialect compatibility; the SPZ importer | dialects ✅ ([PLY_DIALECTS.md](../reference/PLY_DIALECTS.md)); SPZ importer 🚧 — [current.md](current.md) |
| P3 | Tighter anisotropic bounds; worker-thread workaround removal | ⬜ USD contract evolution; implementation debt |

## Milestone ladder

Release-plan mapping: M5 is [v0.3.0](release-plan.md); the glTF/GLB and
SOG M1 entries are [v0.5.0](release-plan.md) candidates; SOG M2-M4 is
post-1.0 work.

- 🚧 **M5 — `gaussian-spz`.** Active as [v0.3.0](release-plan.md); the task
  breakdown is in [current.md](current.md). Select and pin a decoder
  dependency, decode into `GaussianCloudData`, reuse the PLY USD contract,
  and add tolerance-aware PLY/SPZ equivalence tests plus real-size
  performance measurements.
- ⬜ **glTF/GLB Gaussian ADR.** Decide plugin ownership, ordinary scene-graph
  scope, Gaussian extension scope, external resources, and SPZ extension
  interaction before creating a bundle.
- ⬜ **glTF/GLB vertical slice.** Implement only the scope approved by the ADR.
- ⬜ **SOG M1 — one object.** Decode one SOG data object through the common model.
- ⬜ **SOG M2-M4 — composition.** Multi-file packaging, streamed LOD, spatial
  chunks, payloads, and deferred loading.

## PLY compatibility

The core dialect scope shipped with v0.2.0; the observed results and
decisions (canonical names only, no aliases) live in
[PLY_DIALECTS.md](../reference/PLY_DIALECTS.md) and the
[v0.2.0 release record](../releases/v0.2.0.md). Only the unscheduled
remainder stays here:

- ⬜ Investigate binary big-endian support; claim it only after a deterministic
  fixture passes on all supported platforms.

## Performance and loading

The design policy (§12.2) fixes the optimization order: measure first, then
metadata-only reads; streaming comes last. The copy-reduction ladder, the
§12.1 baselines, and metadata-only reads shipped with v0.2.0 and are recorded
in the [delivery history](../reports/delivery-history.md) and
[PERFORMANCE_BASELINES.md](../reference/PERFORMANCE_BASELINES.md); temporary
allocation size remains unmeasured.

- 🚧 Benchmark harness:
  [tools/benchmark_import.py](../../plugins/gaussian-ply/tools/benchmark_import.py)
  measures the §12.1 end-to-end set (CanRead, metadata-only, stage open,
  flatten, peak resident) per asset; the finer file-read / semantic-decode /
  USD-authoring split remains v0.7.0 work.
- ⬜ Investigate memory mapping only after the benchmark identifies I/O or copy
  cost as material.
- ⬜ Investigate chunked decoding, payload composition, and lazy access as SOG
  requirements, not as speculative complexity, and only after the preceding
  improvements are measured.

## Implementation debt

- ⬜ Revisit the worker-thread `SdfChangeBlock` workaround in
  `GaussianPlyFileFormat::Read` (design policy §7.5). Track OpenUSD releases
  after 26.05 and remove the `std::async` hop when detached-stage authoring
  under an outer change block becomes safe; keep the behavior documented and
  tested until then.

## USD contract evolution

- ⬜ Decide how authoritative source axis/unit metadata overrides the PLY policy
  defaults in formats that define those values.
- ⬜ Implement a tighter rotated anisotropic Gaussian AABB only if profiling
  shows the conservative three-sigma bound materially affects viewport
  framing, culling, Hydra scene-index performance, or large-scene traversal.
- ⬜ Define multiple-cloud naming, hierarchy, and extent behavior.
- ⬜ Define camera and training-metadata ownership for source formats that carry
  them.
- ⬜ Evaluate time-sampled or animated Gaussian data separately from static
  file-format import.
- ⬜ Track OpenUSD Gaussian schema changes across the declared version range and
  add a second verified OpenUSD point when a runtime becomes available.

## Packaging and release

- ⬜ Add an aggregate `usd-3dgs-plugins-<version>-<target>` artifact when a
  second bundle exists and OpenStrata has a clear aggregate-product contract.
- ⬜ Publish plugin packages with SBOM and provenance evidence.
- ⬜ Add clean-machine manual install tests for each supported OS.
- ⬜ Document and verify dependency-update procedures for tinyPLY and future
  format libraries.

## Rendering and ecosystem integration

The rendering integration preview ([release plan](release-plan.md) v0.9.0) is
delivered by the sibling project
[hydra-merlin](https://github.com/animu-sphere/hydra-merlin); the file-format
plugins remain renderer-neutral.

- ⬜ Maintain a renderer capability note as Hydra implementations adopt
  `ParticleField3DGaussianSplat`, starting with hydra-merlin.
- ⬜ Add image-based conformance only in a renderer-specific repository
  (hydra-merlin) or optional adapter; the file-format plugins remain
  renderer-neutral.
- ⬜ Add examples that demonstrate downstream inspection without making a
  renderer a core dependency.

## Research

- ⬜ Investigate USD-to-PLY export and define a loss report before promising a
  write dialect.
- ⬜ Investigate SPLAT and KSPLAT only after demand, licensing, and semantic
  overlap are documented.
- ⬜ Evaluate compressed USD representations without changing the canonical
  in-memory semantic model.

## Non-goals

- A universal PLY mesh or point-cloud importer.
- A bundled Hydra renderer.
- Automatic inference of an undocumented coordinate system.
- Per-format copies of the USD authoring contract.
- Floating third-party dependency revisions.
- Silent repair of malformed Gaussian data that changes semantic meaning.
- A streaming architecture ahead of measured need.
- Renderer-specific material or shading policy.
- Bidirectional conversion before read interoperability is stable.

