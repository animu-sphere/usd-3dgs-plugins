# Current

Updated: 2026-07-25
Scope: post-v0.5.0 near-term work

## Current position

v0.5.0 added PlayCanvas SOG v2 import in both supported layouts:

- bundled `.sog` archives;
- unbundled `meta.json` files with lossless WebP companion planes.

The project now has three primary input families:

- Graphdeco-style Gaussian PLY;
- Niantic SPZ v1-v3;
- PlayCanvas SOG v2.

Each format-specific reader and decoder produces the shared
`GaussianCloudData` model. The shared `GaussianLayerWriter` authors that model
as `ParticleField3DGaussianSplat` in the USD stage.

The main result of v0.5.0 is not only the addition of a third format. The
shared model, validation contract, diagnostics, and USD authoring layer from
v0.4.0 proved reusable for a substantially different codebook-and-image
container.

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

## Near-term direction

The next step is not an immediate fourth file format. Work is ordered as
follows:

1. finish the v0.5.0 release polish;
2. improve installation and verification for package consumers;
3. measure performance and safety on large assets;
4. select the next input format;
5. strengthen the integration path to Hydra renderers.

The short-term goal is to make the existing PLY, SPZ, and SOG support reliable
to install, verify, and use before expanding the format matrix.

## v0.5.x — release polish

### Release-state synchronization

README and release records now describe v0.5.0 as released. The remaining
state-synchronization work is to keep the README, release record, capability
matrix, and hosted-verification notes consistent as release evidence changes.

### Binary-consumer path

Keep the developer-oriented build path in the root README, but add a short
package-consumer path before it. The minimum path should cover:

- selecting an artifact for the OS and OpenUSD cycle;
- extracting the package;
- setting `PXR_PLUGINPATH_NAME`;
- checking plugin discovery;
- opening `.ply`, `.spz`, and `.sog` inputs;
- explaining that stock `usdview` can open the stage but does not render the
  splats without a Gaussian-capable Hydra delegate.

Detailed installation instructions belong in
[docs/guides/INSTALL.md](../guides/INSTALL.md); the root README should show
only the shortest working path.

### SOG usage examples

Make the SOG release easier to understand by documenting:

- opening a bundled `.sog`;
- opening an unbundled `meta.json`;
- converting to `.usdc` with `usdcat`;
- the `/Asset/Splat` scene graph;
- an example of `customData.gs`;
- the relationship between the SOG source, USD stage, and rendered result.

The usdview previews are now linked from the root README. A rendered example
must continue to identify the Hydra delegate used, because this repository
owns import and USD authoring, not rendering.

### v0.5.1 position

Treat v0.5.1 as a patch release for public quality rather than a new-format
release. Its expected scope is:

- documentation-state synchronization;
- installation UX;
- SOG performance baselines;
- package smoke-test improvements;
- small diagnostic or display improvements;
- compatibility fixes found during verification.

## Performance and large assets

Add a SOG performance baseline using the committed real-asset corpus. Record,
where measurable:

- input file size and Gaussian count;
- ZIP directory and entry extraction time;
- WebP decode time;
- metadata parsing time;
- codebook and dequantization time;
- shared semantic validation time;
- USD authoring time;
- total stage-open time;
- metadata-only read time;
- peak memory;
- generated USD layer size.

Compare the same or closely matched Gaussian clouds across PLY, SPZ, bundled
SOG, unbundled SOG, and flattened USDC where practical. The purpose is to
locate bottlenecks, not to establish a simplistic format ranking.

In addition to committed fixtures and corpus assets, maintain local validation
targets at approximately 8K, 100K, 1M, and, where practical, 5M or more
Gaussians. Do not commit very large assets; record their source, digest,
license, and validation date instead.

## v0.6.0 — production import hardening

Prioritize import-pipeline robustness over another input format.

### Shared import statistics

Expose a common import-statistics structure for every importer. Candidate
fields include:

- source format and version;
- Gaussian count and SH degree;
- rejected Gaussian count;
- opacity-threshold applications;
- warning count;
- decoded byte count;
- decode and authoring time;
- coordinate conversion;
- source bounds and authored extent.

The structure should be usable from diagnostics, tests, and any future CLI
without creating format-specific statistics APIs.

### Limits for large or hostile input

Define and test shared limits for:

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

Choose the next format only after the v0.5.x follow-up and v0.6.0 foundation
work have been evaluated.

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

### Highest priority

- ✅ Update the README's v0.5.0 state to released.
- ✅ Update the status in `docs/releases/v0.5.0.md`.
- ⬜ Synchronize the capability matrix with hosted-verification evidence.
- ⬜ Add the shortest binary-install path to the README.
- ⬜ Add bundled and unbundled SOG usage examples.

### Next

- ⬜ Measure the SOG performance baseline.
- ⬜ Confirm the package-consumer smoke test.
- ✅ Add SOG, PLY, and SPZ usdview screenshots or an equivalent visual example.
- ⬜ Document manual release-artifact verification.
- ⬜ Finalize the v0.5.1 scope.

### v0.6.0 preparation

- ⬜ Design the shared import-statistics API.
- ⬜ Define hostile-input limits.
- ⬜ Create the asset-resolver test matrix.
- ⬜ Define the large-asset benchmark corpus.
- ⬜ Investigate specifications, fixtures, and licenses for next-format candidates.

## Proposed milestones

### v0.5.1 — Release polish

- documentation-state synchronization;
- binary installation path;
- SOG usage examples;
- SOG performance baseline;
- package-verification fixes.

### v0.6.0 — Production import hardening

- shared import statistics;
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
