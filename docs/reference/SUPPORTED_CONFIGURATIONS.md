# Supported configurations

This document separates what has been exercised from what is only declared for
CI. Anything outside these bounds may work but is not part of the initial
support contract.

## OpenUSD and OpenStrata

| Item | Contract |
| --- | --- |
| OpenStrata | 0.20.0 (v0.1.0-v0.4.0 were built with 0.18.0) |
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
  spherical harmonics dequantization — decoded verbatim into the model's
  native-RUB reference frame ([ADR 0001](../adr/0001-model-frame-is-rub.md));
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

## SOG input contract

Supported:

- SOG v2 (`meta.json` `"version": 2`) in both layouts — the bundled `.sog` ZIP
  archive, and an unbundled `meta.json` whose property planes are loaded as
  resolver-backed companion files from its directory;
- 8-bit RGBA **lossless** WebP property planes, stored or DEFLATE-compressed
  inside the archive;
- position (16-bit split precision across a per-axis log-domain range), scale
  (log-domain codebook), rotation (smallest-three with the largest-component
  tag), opacity (`sh0` alpha), DC (`sh0` codebook), and higher-order SH
  (palette labels into centroid texels and a shared codebook) — converted from
  SOG's PLY-native Graphdeco columns into the model's RUB reference frame
  ([ADR 0001](../adr/0001-model-frame-is-rub.md));
- SH degrees 0-3, which is SOG v2's entire `bands` range;
- metadata-only reads from `meta.json` alone.

Not supported or not verified:

- legacy SOG v1 (no `version` field, per-channel `mins`/`maxs`) — rejected with
  the unsupported-version diagnostic;
- streamed SOG: `lod-meta.json`, spatial chunks, LOD levels, deferred or partial
  loading;
- lossy WebP property planes — rejected rather than decoded approximately;
- writing or exporting SOG;
- network resource loading, streaming, partial reads, or memory mapping.

PLY/SOG cross-format equivalence *is* verified by synthetic triples that encode
one source model into all three formats; see [EQUIVALENCE.md](EQUIVALENCE.md).

Because the stock unbundled layout is named `meta.json`, `gaussian-sog`
registers `.json` as a primary `SdfFileFormat` extension alongside `.sog`. That
claim is process-wide: only one plugin can be the primary format for an
extension, so if another plugin in the same runtime also claims `.json`,
OpenUSD resolves the collision by registration order and warns. `CanRead()`
declines anything that is not a SOG v2 metadata document, so a `.json` this
bundle wins but does not recognise fails with `GSSOG-E002` rather than being
mis-imported — but the other plugin never sees the file. Install `gaussian-sog`
deliberately in an environment that already has a `.json` format registered.

## Output contract

Every bundle is a read-only shared-library `SdfFileFormat` plugin. Each authors
one `ParticleField3DGaussianSplat` under `/Asset/Splat`; `/Asset` is the default
prim. The output contract is described in [PLY_MAPPING.md](PLY_MAPPING.md),
[SPZ_MAPPING.md](SPZ_MAPPING.md), and [SOG_MAPPING.md](SOG_MAPPING.md); the
authored stage is identical because all of them author through the shared
`gaussianUsd` writer.

This repository supplies data interoperability, not a renderer. Visible splat
rendering depends on the active Hydra implementation.
