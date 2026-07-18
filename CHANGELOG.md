# Changelog

All notable user-visible changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
semantic versioning for tagged releases.

## [Unreleased]

### Fixed

- Windows packages now rebuild byte-identically: MSVC compiles, archives, and
  links with `/Brepro`, so object files, `gaussianCore.lib` members, the PE
  header, and the debug directory no longer embed wall-clock build time. Two
  fully clean local build+package cycles produce the same archive digest.

### Changed

- Reduced read-path memory copies: PLY properties decode directly to `float`
  instead of `double`, every intermediate representation (tinyPLY buffers,
  property columns, the Gaussian cloud) is released as soon as the next one is
  built, and the generated layer is transferred directly instead of being
  serialized to USDA and reparsed. On a 696k-Gaussian SH-degree-3 capture this
  cut peak commit memory from 8.05 GiB to 0.40 GiB and `Usd.Stage.Open` from
  15.3 s to 1.9 s; authored stage content is unchanged apart from the layer
  `doc` no longer embedding the round-trip provenance line. A missing or
  size-mismatched vertex property now reports which property is at fault
  instead of the generic non-finite-value error.
- Consolidated the development policy across the documentation: the phased
  format roadmap and priority ladder, the measure-first performance policy and
  optimization order, the metadata-only read policy, the extent policy, the
  worker-thread `SdfChangeBlock` workaround record, and the reserved
  `gaussianUsd` authoring library.

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
