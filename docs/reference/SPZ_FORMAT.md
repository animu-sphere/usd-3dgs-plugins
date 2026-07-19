# SPZ format scope and decisions

This records the specification source, licence review, and scope decisions the
v0.3.0 [release plan](../roadmap/release-plan.md#v030--spz-import) requires
*before* SPZ decoding is implemented. The format-independent model the decoder
targets is [GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md); the
SPZ-specific semantic mapping gets its own `SPZ_MAPPING.md` alongside the
decoder, mirroring [PLY_MAPPING.md](PLY_MAPPING.md).

Status: decisions accepted 2026-07-20. Implementation not started.

## 1. Specification source

SPZ is published by Niantic Labs at
<https://github.com/nianticlabs/spz> under the **MIT licence**. The repository
contains both a reference implementation and a prose format description in its
`README`, so the binary layout is **documented, not inferred**. This satisfies
the standing rule that undocumented binary layouts are never guessed
([backlog](../roadmap/backlog.md), v0.5.0 criteria).

The specification is the normative source for this bundle. Where the reference
implementation and the prose disagree, the discrepancy is recorded here and
resolved deliberately rather than by silently matching the code.

## 2. Decision — implement from the specification

**Accepted: implement the container reader and dequantization in this
repository; use a compression library only for the decompression step.**

Rejected alternatives:

| Option | Why not |
| --- | --- |
| Vendor `nianticlabs/spz` wholesale | Its API reports failure as a bool/empty result, which cannot express the malformed / unsupported-version / internal distinction the release requires of `GSPZ-****`. Diagnostics are a release criterion, not a nicety. |
| Vendor it as a test-only oracle | Viable, and reconsidered if equivalence testing proves hard to ground otherwise, but it adds a build-configuration and licence-management seam for benefit the PLY/SPZ equivalence fixtures already provide. |

This mirrors how PLY is structured: `tinyply` is vendored for *container*
parsing only, and every Gaussian-specific decision lives in this repository's
decoder. SPZ gets the same split — decompression is a library concern, the
format is ours.

Consequence for the architecture: SPZ parsing and dequantization live in
`plugins/gaussian-spz/src/io/`, mirroring the PLY
reader / decoder / diagnostics split. USD prim construction stays in
`libs/gaussian-usd`, which was extracted for exactly this reason.

## 3. Decision — support versions 1-3 in v0.3.0

**Accepted: v0.3.0 reads SPZ versions 1, 2, and 3. Version 4 is deferred.**

The two generations differ structurally:

| | v1-v3 | v4 |
| --- | --- | --- |
| Header | 16 bytes | 32 bytes |
| Compression | gzip, single stream | ZSTD, independent per-attribute streams |
| Layout | attribute-major within one stream | table of contents + N streams |
| Dependency | zlib | zstd |

v1-v3 is a single decompression dependency and one linear layout, which is the
right size for a release whose stated purpose is *proving the shared
architecture supports a compressed second format* — not maximizing format
coverage. v4 is tracked for [v0.5.0](../roadmap/release-plan.md) (expanded
format compatibility), where it is an additive change behind the same
container-version check rather than a rewrite.

**Version 4 files must fail with a specific unsupported-version diagnostic, not
a malformed-container one.** The distinction is a release criterion: a user
handed a v4 file needs to be told the version is not supported yet, not that
their file is corrupt. The `GSPZ-****` catalog therefore separates
*unsupported* from *malformed* from the outset.

## 4. Container facts to validate against fixtures

Recorded from the specification for implementation; every one of these is
confirmed against a real file before the decoder is considered done, and any
correction is made here first.

- The file is gzip as a whole: it begins with the `0x1f 0x8b` member header,
  and the magic `0x5053474e` is the first field of the *decompressed* stream,
  not of the file. Detection has to account for that (see §6).
- Header fields are little-endian: magic, version, point count, SH degree,
  fractional bits, flags, reserved.
- Positions: 24-bit fixed-point signed integers; the fractional-bit count comes
  from the header rather than being fixed.
- Scales: 8-bit, log-encoded.
- Rotations: smallest-three quaternion encoding — the stored component order
  and the reconstruction of the dropped component are the highest-risk part of
  the decoder and get dedicated fixtures.
- Alphas and colours: 8-bit unsigned.
- Spherical harmonics: 8-bit signed, count determined by degree.
- Attribute-major ordering: positions, scales, rotations, alphas, colours,
  spherical harmonics.

## 5. Coordinate system

SPZ conventionally stores right-up-back (RUB); the Graphdeco PLY frame this
project already authors is right-down-front (RDF). Per
[GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md) §2 the PLY-native
frame is the model's reference frame, so **the SPZ decoder converts RUB into
it**. Passing SPZ through unconverted would place equivalent PLY and SPZ assets
in different orientations on the same stage, which the v0.3.0 equivalence
criterion forbids.

The conversion is part of decoding and is covered by the PLY/SPZ equivalence
fixtures, which would otherwise disagree on sign in two axes.

## 6. Open items

- Confirm the SH quantization bit-depth actually present in real v1-v3 assets;
  the packing bit-depth is an encoder option, and the decoder must not assume
  the default.
- Confirm degree-4 handling. The header field admits SH degrees the shared
  model does not currently carry (`GaussianCloudData` covers degrees 0-3); if
  real v1-v3 files use degree 4, this needs an explicit accept-or-reject
  decision rather than a silent truncation.
- Identify a legally redistributable SPZ asset with recorded provenance for the
  corpus, per the release criteria.
- Decide the `CanRead()` strategy. The container magic sits *inside* the gzip
  member, not at file offset 0, so the header-only rule of design policy §7.6
  has no signature to read directly. The two candidates are accepting on
  extension plus the `0x1f 0x8b` gzip marker — which claims every gzip file
  with the right extension, the over-broad direction §7.6 warns about — or
  inflating only the first header-sized block. This must be settled before the
  reader is written, not discovered during it.
