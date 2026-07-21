# Changelog

All notable user-visible changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
semantic versioning for tagged releases.

## [Unreleased]

Development target: **v0.4.0 — Gaussian Import Foundation** — no new end-user
format; the decoder-to-USD seam proven by PLY and SPZ becomes a formal,
reusable contract (normative model contract, shared semantic validation, the
coordinate-system ADR, a decoder test kit) before a third format depends on it
([release plan](docs/roadmap/release-plan.md)).

### Changed

- The [Gaussian model contract](docs/reference/GAUSSIAN_MODEL_CONTRACT.md) is
  revised into the normative decoder contract: supported SH degrees pinned to
  0-3, required arrays and length relationships, maximum-count and overflow
  policy, retained-source-metadata rules, and the SOG encodings named among
  the representations that never enter the shared model.
- Shared cloud validation now enforces the contract's SH degree ceiling and
  quaternion normalization (tolerance `1e-4`); both were previously pinned
  only by the test-side contract checker.
- A Gaussian PLY declaring SH degree 4 or higher (a well-formed but
  unsupported layout) is now rejected with the new diagnostic `GSPLY-E017`
  instead of being imported. SPZ behavior is unchanged; its degree ceiling now
  reads from the shared constant.

## [0.3.0] - 2026-07-20

Read-only SPZ import through the shared `GaussianCloudData` pipeline, plus the
post-v0.2.0 stabilization it rests on: the release-plan Phase 2 theme
([release plan](docs/roadmap/release-plan.md)). The architectural claim is that
a second, compressed format reaches USD through the same
`GaussianCloudData` → `GaussianLayerWriter` path PLY uses, contributing no
format-specific USD authoring.

### Changed

- Documentation records v0.1.0 and v0.2.0 as tagged and published, tracks
  v0.3.0 as the active development target, and the README quick start now
  leads with the plain CMake build.
- The USD authoring layer moved from `plugins/gaussian-ply/src/usd/` into the
  shared `libs/gaussian-usd` library, so every format bundle authors Gaussian
  stages through one implementation. Authored stage output and the
  `GSPLY-****` diagnostic codes are unchanged; this is an internal
  restructuring ahead of SPZ import.

### Added

- [Gaussian model contract](docs/reference/GAUSSIAN_MODEL_CONTRACT.md): the
  normative, format-independent description of `GaussianCloudData` that every
  decoder targets.
- [SPZ format scope](docs/reference/SPZ_FORMAT.md): specification source,
  licence review, and the accepted v0.3.0 scope — implement from the
  specification, support SPZ versions 1-3, defer version 4.
- A shared model-contract checker in `gaussianCore`
  (`openstrata/gs/testing/CloudContract.h`), so every decoder is held to the
  same code rather than to a per-bundle copy of it.
- A documented `CanRead()` contract (design policy §7.6): it reports plausible
  format compatibility, not asset validity.
- The `gaussian-spz` bundle with the SPZ v1-v3 container reader: signature
  detection for both the gzip-wrapped v1-v3 layout and the plaintext v4
  layout (rejected with a specific unsupported-version diagnostic), gzip
  member framing, overflow-safe header and size validation, truncation,
  corruption, and trailing-data detection, a metadata-only header path, and
  the stable `GSPZ-****` container diagnostic catalog.
- Read-only SPZ import: opening an `.spz` file authors a Gaussian Splatting
  USD stage. The decoder dequantizes positions (v1 float16, v2/v3 24-bit
  fixed point), 8-bit log scales and opacity, per-version rotations
  (first-three and smallest-three), and spherical harmonics, converts SPZ's
  right-up-back frame into the model's right-down-front reference frame, and
  authors through the shared `gaussianUsd` writer — so PLY and SPZ produce the
  identical stage hierarchy, schema, and metadata. SH degree 4 is reported as
  unsupported (not malformed); the mapping and the reference quantization
  constants are documented in
  [SPZ mapping](docs/reference/SPZ_MAPPING.md).
- Vendored miniz 3.0.2 (MIT) for the raw-DEFLATE decompression and CRC32
  behind the SPZ container reader; the gzip framing itself is parsed by the
  reader so container diagnostics keep their required granularity.
- A real-asset SPZ corpus: two CC0-1.0 author-captured Scaniverse exports
  (`yashica-t4`, `leica-sofort`) subset to 8,192 Gaussians by
  `scripts/spz_subset.py`, with source and output checksums, SPZ version, SH
  degree, and crop parameters recorded per asset.
- [Performance baselines](docs/reference/PERFORMANCE_BASELINES.md) for SPZ,
  measured through the same `scripts/benchmark_import.py` seam as PLY so the
  two tables cannot drift apart.
- [PLY/SPZ cross-format equivalence tests](docs/reference/EQUIVALENCE.md).
  `tools/generate_equivalence_fixtures.py` defines one source model in
  shared-model space and encodes it into both formats; `tests/equivalence/`
  decodes both and compares every model attribute at tolerances derived from
  the SPZ quantization steps. This is what holds the RUB→RDF conversion, the
  15 spherical-harmonic band sign flips, and the two SH memory layouts to an
  independent witness rather than to a re-derived formula. Pairs cover SPZ v2
  and v3 against one shared PLY, so a v3-only failure isolates the
  smallest-three rotation path.

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
