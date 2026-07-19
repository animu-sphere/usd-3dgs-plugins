# Changelog

All notable user-visible changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
semantic versioning for tagged releases.

## [Unreleased]

Development target: **v0.3.0 — SPZ import** — post-v0.2.0 stabilization plus
read-only SPZ import through the shared `GaussianCloudData` pipeline
([release plan](docs/roadmap/release-plan.md)).

### Changed

- Documentation records v0.1.0 and v0.2.0 as tagged and published, tracks
  v0.3.0 as the active development target, and the README quick start now
  leads with the plain CMake build.

## [0.2.0] - 2026-07-19

Production-ready Graphdeco PLY import: the release-plan Phase 1
stabilization theme.

### Added

- Metadata-only reads: `Read(metadataOnly=true)` authors the `/Asset`
  scaffold, stage metrics, and header-derived Gaussian count and SH degree
  without decoding vertex data (~5 ms at any asset size).
- File-format arguments `shDegree` (cap the imported SH degree, 0-3),
  `opacityThreshold` (drop Gaussians below a decoded-opacity threshold,
  0-1), and `scaleMultiplier` (rescale linear scales, > 0), each validated
  with stable diagnostics and covered by unit and integration tests.
- Stable diagnostic identifiers: every error and warning starts with a
  `[GSPLY-****]` code that is never renumbered or reused, with a
  machine-readable catalog shipped in the plugin resources
  (`diagnostics.json`) and cross-checked by tests.
- Fixture coverage for degree-2 and degree-3 SH with exact coefficient
  assertions, a three-Gaussian asymmetric fixture, a scrambled
  property-order fixture, and malformed cases for every header-layout
  diagnostic.
- Documented dialect compatibility observed against real exporters
  (`docs/reference/PLY_DIALECTS.md`): Graphdeco reference (Mip-NeRF 360
  `garden`, 5.83M Gaussians), Brush (lexicographic `f_rest` declaration
  order), Postshot, and the explicit unsupported-input table.
- Design-policy §12.1 performance baselines
  (`docs/reference/PERFORMANCE_BASELINES.md`) measured with the new
  `tools/benchmark_import.py`, spanning 3 to 5.83M Gaussians.
- Release-version single-sourcing: `scripts/release.py set-version`
  rewrites every declared version location, and the release guard now fails
  on drift in `openstrata.toml` and bundle CMake project versions.
- `CONTRIBUTING.md` with the minimal development path, and
  `docs/guides/BUILDING.md` carrying the full build/test surface.

### Fixed

- Multi-million-Gaussian PLYs failed to load in v0.1.0: the read path
  serialized the generated stage to USDA text and reparsed it, and the
  multi-gigabyte round-trip failed at scale (observed with the 5.8M-splat
  `garden` reference). The layer is now authored directly and transferred
  without a text round-trip.
- Windows packages now rebuild byte-identically: MSVC compiles, archives, and
  links with `/Brepro`, so object files, `gaussianCore.lib` members, the PE
  header, and the debug directory no longer embed wall-clock build time. Two
  fully clean local build+package cycles produce the same archive digest.

### Changed

- Reduced read-path memory copies: PLY properties decode directly to `float`
  instead of `double`, and every intermediate representation (tinyPLY
  buffers, property columns, the Gaussian cloud) is released as soon as the
  next one is built. On a 696k-Gaussian SH-degree-3 capture this cut peak
  commit memory from 8.05 GiB to 0.40 GiB and `Usd.Stage.Open` from 15.3 s
  to 1.9 s; authored stage content is unchanged apart from the layer `doc`
  no longer embedding the round-trip provenance line. A missing or
  size-mismatched vertex property now reports which property is at fault
  instead of the generic non-finite-value error.
- Consolidated the development policy across the documentation: the phased
  format roadmap and priority ladder, the measure-first performance policy and
  optimization order, the metadata-only read policy, the extent policy, the
  worker-thread `SdfChangeBlock` workaround record, and the reserved
  `gaussianUsd` authoring library.
- Reworked the README around a quick start (what it does, what it reads, how
  to run it); build, test, and release detail moved to the guides.

## [0.1.0] - 2026-07-19

First tagged release: a read-only Gaussian PLY vertical slice, observed on
hosted Windows, macOS arm64, and Linux.

### Added

- OpenStrata `usd-plugin-workspace` with the `gaussian-ply` file-format bundle.
- ASCII and binary little-endian Graphdeco Gaussian PLY decoding through
  vendored tinyPLY 2.3.4.
- Format-independent `gaussianCore` model, math, validation, and workspace
  library descriptor.
- OpenUSD 26.05 `ParticleField3DGaussianSplat` authoring at `/Asset/Splat`.
- Position, logarithmic-scale, scalar-first quaternion, opacity-logit, and
  spherical-harmonic conversion.
- Deterministic C++, Python, negative, binary, and OST golden tests.
- Target-specific OpenStrata packaging with third-party notice.
- Generated Windows/macOS/Linux PR CI contract, passing on hosted runners.
- Tag-driven release workflow deriving its matrix from `openstrata.ci.yaml`,
  gating on version agreement and digest-reproducible packaging.
- English architecture, guide, reference, roadmap, report, and release docs.

### Limitations

- Read-only, fully materialized PLY import.
- Canonical Graphdeco property names only.
- No bundled Gaussian renderer.
- Windows is verified to OST L4; macOS and Linux to L5. The cap is inherited
  from the reference workspace's hosted multiline-USDA line-ending finding.
- Package-origin L5 skips rather than executes; the bundle manifest has no
  golden declaration. Upstream packaging seam, tracked in the roadmap.
- No real-asset corpus or performance measurement yet.
