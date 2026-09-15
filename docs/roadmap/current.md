# Current

Updated: 2026-09-16
Scope: v0.6.0 preparation

## Current position

The project supports PlayCanvas SOG v2 import in both supported layouts:

- bundled `.sog` archives;
- unbundled `meta.json` files with lossless WebP companion planes.

The project now has three primary input families:

- Graphdeco-style Gaussian PLY;
- Niantic SPZ v1-v3;
- PlayCanvas SOG v2.

Each format-specific reader and decoder produces the shared
`GaussianCloudData` model. The shared `GaussianLayerWriter` authors that model
as `ParticleField3DGaussianSplat` in the USD stage.

The shared model, validation contract, diagnostics, USD authoring layer, import
statistics, and safety limits now cover all three format bundles. The active
work is production import hardening before selecting another input format.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

## Near-term direction

The next step is not an immediate fourth file format. Work is ordered as
follows:

1. complete resolver and hostile-input coverage;
2. define and measure large-asset validation targets;
3. improve package-consumer replacement and uninstallation checks;
4. select the next input format;
5. strengthen the integration path to Hydra renderers.

The short-term goal is to make the existing PLY, SPZ, and SOG support reliable
to install, verify, and use before expanding the format matrix.

## v0.6.0 — production import hardening

Prioritize import-pipeline robustness over another input format.

### Limits for large or hostile input

Define and test shared limits for; the initial shared count and container
budgets are recorded in [IMPORT_LIMITS.md](../reference/IMPORT_LIMITS.md):

- Gaussian count and plane dimensions;
- ZIP entry count and expanded size;
- integer overflow and allocation sizes;
- codebook and palette counts;
- JSON nesting and token counts;
- compression-bomb behavior;
- companion files reached through an asset resolver;
- duplicate and missing planes.

Prefer shared validation and checked-size utilities over separate copies of the
same protection in each format bundle.

### Diagnostics and observability

Continue the stable diagnostic-code policy while improving:

- import summaries;
- warning aggregation;
- source and companion-path reporting;
- CI cross-checks for machine-readable diagnostic catalogs;
- an index searchable by diagnostic code;
- the boundary between common and format-specific diagnostics.

### Asset-resolver verification

Use unbundled SOG as the driver for a documented resolver test matrix covering
relative, absolute, package-relative, search-path, and custom-resolver cases,
as well as missing companions, case sensitivity, Windows paths, URI-like
identifiers, and metadata-only reads that must not open property planes.

### Package-consumer tests

Test generated packages from a clean consumer environment, not only from the
development tree. The consumer gate should cover plugin discovery, dependent
shared libraries, resource catalogs, schema availability, `usdcat`,
`usdchecker`, Python and C++ stage opens, all three formats, and replacement or
uninstallation behavior.

## Next format candidates

Choose the next format only after the v0.6.0 foundation work has been
evaluated.

### First candidate: SuperSplat compressed PLY

Reasons to investigate it first:

- it is distributed with a `.ply` extension and can be confused with ordinary
  Gaussian PLY;
- the current importer explicitly rejects it, so the user value is clear;
- it extends the existing PLY dialect and detection work;
- it may reuse the current PLY plugin without changing the shared model.

The claim gate must distinguish it strictly from canonical Graphdeco PLY. Its
compressed layout should remain a format-specific reader rather than becoming
an accidental mode of the ordinary PLY decoder.

### Second candidate: `.splat`

`.splat` is widely exchanged and may be inexpensive to implement. Its missing
or implicit semantics around provenance, SH, precision, and coordinate systems
must be documented before support is claimed. “Can be decoded” and “can be
decoded with preserved meaning” remain separate acceptance criteria.

### Third candidate: `.ksplat`

`.ksplat` may benefit GaussianSplats3D users as a compressed distribution
format. Investigation must cover version and compression dependencies,
specification stability, independent implementability, fixtures, and license
status.

### Later candidates

- streamed SOG and LOD chunks;
- LCC / LCC2;
- glTF / GLB Gaussian extensions.

Streamed SOG is a larger composition milestone involving LOD, chunk lifecycle,
resolver behavior, and multiple payloads. It should not be treated as a small
extension of one-object import. glTF Gaussian extensions should be reconsidered
as their specification and ecosystem mature.

## Hydra boundary

`usd-3dgs-plugins` owns import and USD authoring. Rendering remains owned by
the sibling project [hydra-merlin](https://github.com/animu-sphere/hydra-merlin).

The user-facing integration should still be improved through documentation
and tests covering:

- compatible OpenUSD versions;
- the shared schema contract;
- sample stages and source assets;
- importer-to-renderer end-to-end smoke tests;
- reciprocal README links;
- known renderer limitations;
- the difference between stock `usdview` and a Gaussian-capable delegate.

Do not move renderer implementation into this repository. Keep the boundary
explicit through integration tests and documentation.

## Release operations

Keep the tag-driven, digest-reproducible, SBOM-backed release process. Add or
standardize:

- a release checklist;
- a documentation-state check;
- artifact-install smoke tests;
- checksum verification;
- release-note and `CHANGELOG.md` consistency checks;
- release-tag-qualified capability claims;
- explicit known limitations;
- a distinction between hosted and local verification;
- benchmark environment records.

Release records should state not only what was implemented, but also which
real environments and package paths were actually verified.

## Immediate actions

- 🚧 Broaden hostile-input fixture coverage beyond the implemented shared count
  and reader resource budgets.
- ⬜ Create the asset-resolver test matrix.
- ⬜ Define the large-asset benchmark corpus.
- ⬜ Investigate specifications, fixtures, and licenses for next-format candidates.

## Proposed milestones

### v0.6.0 — Production import hardening

- large-input limits;
- resolver test coverage;
- package-consumer verification;
- improved diagnostics and summaries;
- expanded benchmarks.

### v0.7.0 — Next format

SuperSplat compressed PLY is the first candidate. The final choice requires a
published or independently implementable specification, redistributable
fixtures, license clearance, and a viable cross-format equivalence test.

## Decision criteria

Evaluate every proposed format against the following criteria:

1. Is it actually in use?
2. Is there a public specification or independently implementable reference?
3. Is the licensing clear?
4. Can deterministic fixtures be created?
5. Can a provenance-recorded real-asset corpus be obtained?
6. Can it map to `GaussianCloudData` without losing meaning?
7. Can cross-format equivalence tests be built?
8. Can stable diagnostics be defined?
9. Does metadata-only read make sense?
10. Can the result be reproduced in a package-consumer environment?

The number of supported formats is not the goal. A format should be accepted
only when its specification, semantics, validation, and distribution story can
be maintained together.
