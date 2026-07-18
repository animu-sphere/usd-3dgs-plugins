# Changelog

All notable user-visible changes are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
semantic versioning for tagged releases.

## [Unreleased]

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
- Generated Windows/macOS/Linux PR CI contract.
- English architecture, guide, reference, roadmap, report, and release docs.

### Limitations

- Read-only, fully materialized PLY import.
- Canonical Graphdeco property names only.
- No bundled Gaussian renderer.
- Hosted cross-platform CI and the first release are not yet completed.
