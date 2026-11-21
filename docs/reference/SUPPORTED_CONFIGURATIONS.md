# Supported configurations

This document separates what has been exercised from what is only declared for
CI. Anything outside these bounds may work but is not part of the initial
support contract.

## OpenUSD and OpenStrata

| Item | Contract |
| --- | --- |
| OpenStrata | 0.23.20 in CI and local verification (v0.5.1 was built with 0.22.10, v0.5.0 with 0.20.0, v0.1.0-v0.4.0 with 0.18.0) |
| OpenUSD tolerated range | `>=26.05,<27.0` |
| OpenUSD exercised locally | 26.08 (26.05 through v0.5.0) |
| OpenUSD in the pinned CI runtimes | 26.08, tag `v26.08` |
| OpenStrata platform/profile | `cy2026` / `usd` |
| Python ABI exercised locally | CPython 3.13 |

OpenUSD does not promise C++ ABI compatibility across versions. Rebuild the
plugin against the target OpenUSD runtime even when the semantic version falls
inside the declared range.

## Build toolchain

| Item | Requirement / exercised value |
| --- | --- |
| CMake | 3.23 or newer at workspace root; 4.4.3 exercised |
| C++ | C++17, compiler extensions disabled |
| Build type | Release by default |
| Build backend | Ninja 1.13.2 exercised |
| Windows compiler | MSVC toolset 143 (14.51 exercised) |

## Platforms

| OS / architecture | CI contract | Observed in this repository |
| --- | --- | --- |
| Windows 2022 / x86_64 / MSVC 143 | build, L0-L4, package | local Windows build, L0-L5, package, package-origin L0-L5, against the same pinned runtime artifact CI uses; hosted release dry-run [#34996154493](https://github.com/animu-sphere/usd-3dgs-plugins/actions/runs/34996154493) passed the declared L4 lane |
| macOS 15 / arm64 / libc++ | build, L0-L5, package | hosted release dry-run [#34996154493](https://github.com/animu-sphere/usd-3dgs-plugins/actions/runs/34996154493) passed build, L0-L5, package, and package-origin verification |
| Ubuntu 24.04 / x86_64 / glibc 2.38+ | build, L0-L5, package | hosted release dry-run [#34996154493](https://github.com/animu-sphere/usd-3dgs-plugins/actions/runs/34996154493) passed build, L0-L5, package, and package-origin verification |

The matrix uses immutable runtime artifact and OCI digests from the reference
`usd-vrm-plugins` workspace. Since 2026-09-05 those are the OpenUSD 26.08
imaging leaves of OpenStrata's own canonical runtime matrix — `26.08-gl` on
Windows and Linux, `26.08-metal` on macOS arm64 — each carrying an SPDX SBOM
and SLSA/in-toto provenance and verifying `trust: attested`. The macOS leaf's
target id now names its deployment target (`macos-arm64-macos130-py313`), and
the Linux leaf sets a glibc 2.38 floor. The 26.05 runtimes used through v0.5.0
were withdrawn upstream, so re-pinning was forced rather than elective; the
reasoning is in the header of [`openstrata.ci.yaml`](../../openstrata.ci.yaml).
`ost ci validate` verifies the matrix and evidence requirements; actual hosted
support is claimed only after those jobs complete.

## PLY input contract

Supported:

- ASCII PLY;
- binary little-endian PLY;
- one scalar `vertex` element containing canonical Graphdeco Gaussian
  properties;
- SH degree inferred from contiguous `f_rest_*` values;
- unknown scalar vertex properties ignored with a warning;
- SuperSplat compressed PLY in binary little-endian: the 18-property `chunk`
  element, the four packed `uint` vertex properties, and an optional `sh`
  element for SH degrees 1-3, routed by header signature and decoded into the
  same model with documented quantization loss
  ([COMPRESSED_PLY_MAPPING.md](COMPRESSED_PLY_MAPPING.md)).

Not supported or not verified:

- binary big-endian PLY;
- ASCII or big-endian compressed PLY, and the 12-property colourless `chunk`
  (rejected with `GSPLY-E020` and `GSPLY-E021`);
- list-valued required properties;
- arbitrary point clouds and polygon meshes;
- user-defined property aliases;
- multiple Gaussian clouds in one source;
- streaming, partial reads, or memory mapping.

## SPZ input contract

Supported:

- SPZ container versions 1, 2, and 3 (gzip-wrapped, single stream) and 4
  (one Zstandard stream per attribute);
- position, scale, rotation (first-three and smallest-three), opacity, and
  spherical harmonics dequantization — decoded verbatim into the model's
  native-RUB reference frame ([ADR 0001](../adr/0001-model-frame-is-rub.md));
- SH degrees 0-3;
- metadata-only reads from the container header.

Not supported or not verified:

- SPZ versions above 4 — rejected with the unsupported-version diagnostic;
- data stored in a declared coordinate frame other than RUB — rejected with
  `GSPZ-E016` rather than converted;
- SPZ v4 assets from real producers — no redistributable capture exists yet,
  so v4 is verified by synthetic fixtures and one reference-written file;
- SH degree 4 — rejected as unsupported (the shared model carries 0-3);
- equivalence pairs for SPZ v1 float16 positions — the v1 position path is
  pinned by the SPZ decoder suite instead, for the reason in
  [EQUIVALENCE.md §5](EQUIVALENCE.md);
- writing or exporting SPZ;
- streaming, partial reads, or memory mapping.

PLY/SPZ cross-format equivalence *is* verified for SPZ v2, v3, and v4 by synthetic
pairs that encode one source model into both formats; see
[EQUIVALENCE.md](EQUIVALENCE.md).

## SOG input contract

Supported:

- SOG v2 and v3 (`meta.json` `"version": 2` or `3`) in both layouts — the bundled `.sog` ZIP
  archive, and an unbundled `meta.json` whose property planes are loaded as
  resolver-backed companion files from its directory;
- 8-bit RGBA **lossless** WebP property planes, stored or DEFLATE-compressed
  inside the archive;
- position (16-bit split precision across a per-axis log-domain range), scale
  (log-domain codebook), rotation (v2 smallest-three with the
  largest-component tag, v3 exponential map), opacity (`sh0` alpha; v3 values
  above 1 are clamped to 1 with a warning), DC (`sh0` codebook), and
  higher-order SH
  (palette labels into centroid texels and a shared codebook) — converted from
  SOG's PLY-native Graphdeco columns into the model's RUB reference frame
  ([ADR 0001](../adr/0001-model-frame-is-rub.md));
- SH degrees 0-3, which is SOG v2's entire `bands` range;
- SOG v3 blind textures, `extras`, and `comments`, and the `model` field in
  both versions, which are read but not imported (blind textures and `model`
  warn);
- metadata-only reads from `meta.json` alone.

Not supported or not verified:

- legacy SOG v1 (no `version` field, per-channel `mins`/`maxs`) — rejected with
  the unsupported-version diagnostic;
- the interim SOG v3 layout of SplatTransform 3.11.0-3.11.1 (no `quats`) —
  rejected with the unsupported-version diagnostic, asking for a re-export;
- SOG v3 with 4 SH bands (SH degree 4) — rejected;
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
declines anything that is not a SOG v2 or v3 metadata document, so a `.json` this
bundle wins but does not recognise fails with `GSSOG-E002` rather than being
mis-imported — but the other plugin never sees the file. Install `gaussian-sog`
deliberately in an environment that already has a `.json` format registered.

## `.splat` input contract

Supported:

- headerless `.splat` files of fixed 32-byte records: float32 position and
  linear scale, 8-bit colour and alpha, and an 8-bit scalar-first rotation,
  decoded with the documented quantization loss
  ([SPLAT_MAPPING.md](SPLAT_MAPPING.md));
- the Graphdeco PLY frame every observed writer uses, converted into the
  model's RUB reference frame with the shared `FlipYZAxes`;
- metadata-only reads from the file size.

Not supported or not verified:

- spherical harmonics above degree 0 — the format stores none;
- `.ksplat` or any record size other than 32 bytes;
- `.splat` captures written by third-party tools beyond SplatTransform's
  MIT test file — no redistributable capture exists, so the corpus is
  converted from the committed CC0 PLY subsets;
- writing or exporting `.splat`;
- streaming, partial reads, or memory mapping.

The bundle claims a file by its `splat` extension and a size that is a
multiple of 32 bytes, because the format has no header; a renamed PLY,
SPZ v4, SOG, or GLB file fails with `GSSPLAT-E003`
([SPLAT_FORMAT.md §4](SPLAT_FORMAT.md)).
PLY/`.splat` cross-format equivalence is verified by synthetic fixtures and by
the CC0 corpus against its source PLY; see [EQUIVALENCE.md](EQUIVALENCE.md).

## Output contract

Every bundle is a read-only shared-library `SdfFileFormat` plugin. Each authors
one `ParticleField3DGaussianSplat` under `/Asset/Splat`; `/Asset` is the default
prim. The output contract is described in [PLY_MAPPING.md](PLY_MAPPING.md),
[SPZ_MAPPING.md](SPZ_MAPPING.md), [SOG_MAPPING.md](SOG_MAPPING.md), and
[SPLAT_MAPPING.md](SPLAT_MAPPING.md); the
authored stage is identical because all of them author through the shared
`gaussianUsd` writer.

This repository supplies data interoperability, not a renderer. Visible splat
rendering depends on the active Hydra implementation.
