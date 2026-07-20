# Supported configurations

This document separates what has been exercised from what is only declared for
CI. Anything outside these bounds may work but is not part of the initial
support contract.

## OpenUSD and OpenStrata

| Item | Contract |
| --- | --- |
| OpenStrata | 0.18.0 |
| OpenUSD tolerated range | `>=26.05,<27.0` |
| OpenUSD exercised locally | 26.05 |
| OpenStrata platform/profile | `cy2026` / `usd` |
| Python ABI exercised locally | CPython 3.13 |

OpenUSD does not promise C++ ABI compatibility across versions. Rebuild the
plugin against the target OpenUSD runtime even when the semantic version falls
inside the declared range.

## Build toolchain

| Item | Requirement / exercised value |
| --- | --- |
| CMake | 3.23 or newer at workspace root; 4.4.0-rc2 exercised |
| C++ | C++17, compiler extensions disabled |
| Build type | Release by default |
| Build backend | Ninja 1.13.2 exercised |
| Windows compiler | MSVC toolset 143 (14.34 exercised) |

## Platforms

| OS / architecture | CI contract | Observed in this repository |
| --- | --- | --- |
| Windows 2022 / x86_64 / MSVC 143 | build, L0-L4, package | local Windows build, L0-L5, package, package-origin L0-L4 |
| macOS 15 / arm64 / libc++ | build, L0-L5, package | declared; first hosted run pending |
| Ubuntu 24.04 / x86_64 / glibc 2.38+ | build, L0-L5, package | declared; first hosted run pending |

The matrix uses immutable runtime artifact and OCI digests from the reference
`usd-vrm-plugins` workspace. `ost ci validate` verifies the matrix and evidence
requirements; actual hosted support is claimed only after those jobs complete.

## PLY input contract

Supported:

- ASCII PLY;
- binary little-endian PLY;
- one scalar `vertex` element containing canonical Graphdeco Gaussian
  properties;
- SH degree inferred from contiguous `f_rest_*` values;
- unknown scalar vertex properties ignored with a warning.

Not supported or not verified:

- binary big-endian PLY;
- list-valued required properties;
- arbitrary point clouds and polygon meshes;
- user-defined property aliases;
- multiple Gaussian clouds in one source;
- streaming, partial reads, or memory mapping.

## SPZ input contract

Supported:

- SPZ container versions 1, 2, and 3 (gzip-wrapped, single stream);
- position, scale, rotation (first-three and smallest-three), opacity, and
  spherical harmonics dequantization with the documented RUB→RDF conversion;
- SH degrees 0-3;
- metadata-only reads from the container header.

Not supported or not verified:

- SPZ version 4 (ZSTD) — rejected with the unsupported-version diagnostic;
- SH degree 4 — rejected as unsupported (the shared model carries 0-3);
- equivalence pairs for SPZ v1 float16 positions — the v1 position path is
  pinned by the SPZ decoder suite instead, for the reason in
  [EQUIVALENCE.md §5](EQUIVALENCE.md);
- writing or exporting SPZ;
- streaming, partial reads, or memory mapping.

PLY/SPZ cross-format equivalence *is* verified for SPZ v2 and v3 by synthetic
pairs that encode one source model into both formats; see
[EQUIVALENCE.md](EQUIVALENCE.md).

## Output contract

Both bundles are read-only shared-library `SdfFileFormat` plugins. Each authors
one `ParticleField3DGaussianSplat` under `/Asset/Splat`; `/Asset` is the default
prim. The output contract is described in [PLY_MAPPING.md](PLY_MAPPING.md) and,
for SPZ, [SPZ_MAPPING.md](SPZ_MAPPING.md); the authored stage is identical
because both author through the shared `gaussianUsd` writer.

This repository supplies data interoperability, not a renderer. Visible splat
rendering depends on the active Hydra implementation.

