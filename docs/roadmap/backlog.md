# Backlog

This is ordered but unscheduled work. The active release milestone is in
[current.md](current.md); completed work is in the
[delivery history](../reports/delivery-history.md).

Legend: ⬜ not started

## Milestone ladder

- ⬜ **M5 — `gaussian-spz`.** Select and pin a decoder dependency, decode into
  `GaussianCloudData`, reuse the PLY USD contract, and add tolerance-aware
  PLY/SPZ equivalence tests plus real-size performance measurements.
- ⬜ **glTF/GLB Gaussian ADR.** Decide plugin ownership, ordinary scene-graph
  scope, Gaussian extension scope, external resources, and SPZ extension
  interaction before creating a bundle.
- ⬜ **glTF/GLB vertical slice.** Implement only the scope approved by the ADR.
- ⬜ **SOG M1 — one object.** Decode one SOG data object through the common model.
- ⬜ **SOG M2-M4 — composition.** Multi-file packaging, streamed LOD, spatial
  chunks, payloads, and deferred loading.

## PLY compatibility

- ⬜ Add degree-2 and degree-3 SH fixtures and exact coefficient assertions.
- ⬜ Add multi-Gaussian fixtures with asymmetric scales and rotations.
- ⬜ Decide and document any property aliases from measured ecosystem usage.
- ⬜ Investigate binary big-endian support; claim it only after a deterministic
  fixture passes on all supported platforms.
- ⬜ Add explicit malformed cases for duplicate SH numeric indices, non-finite
  fields, unsupported list properties, overflowed scale, and mismatched counts.
- ⬜ Add stable diagnostic identifiers and a machine-readable diagnostic catalog.

## Performance and loading

- ⬜ Build a benchmark harness that separates file read, semantic decode, USD
  authoring, and total stage-open time.
- ⬜ Record peak resident memory and temporary allocation size.
- ⬜ Evaluate structure-of-arrays ownership and zero-copy handoff opportunities.
- ⬜ Investigate memory mapping only after the benchmark identifies I/O or copy
  cost as material.
- ⬜ Investigate chunked decoding, payload composition, and lazy access as SOG
  requirements, not as speculative v0.1 complexity.

## USD contract evolution

- ⬜ Decide how authoritative source axis/unit metadata overrides the PLY policy
  defaults in formats that define those values.
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

- ⬜ Maintain a renderer capability note as Hydra implementations adopt
  `ParticleField3DGaussianSplat`.
- ⬜ Add image-based conformance only in a renderer-specific repository or
  optional adapter; the file-format plugins remain renderer-neutral.
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

