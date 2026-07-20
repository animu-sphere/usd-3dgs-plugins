# SPZ format scope and decisions

This records the specification source, licence review, and scope decisions the
v0.3.0 [release plan](../roadmap/release-plan.md#v030--spz-import) requires
*before* SPZ decoding is implemented. The format-independent model the decoder
targets is [GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md); the
SPZ-specific semantic mapping gets its own `SPZ_MAPPING.md` alongside the
decoder, mirroring [PLY_MAPPING.md](PLY_MAPPING.md).

Status: decisions accepted 2026-07-20; container reader implemented
2026-07-20 (`plugins/gaussian-spz/src/io/SpzReader.*`). Semantic decoding not
started.

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

Corrected 2026-07-20 against the upstream README and reference serializer
(`load-spz.cc`) while implementing the container reader. Three of the facts
initially recorded here were wrong: the attribute ordering placed scales and
rotations before alphas and colours, the 24-bit fixed-point position encoding
was recorded without its version qualifier (v1 stores float16), and the
smallest-three rotation encoding was recorded without its version qualifier
(v1-v2 store the first three components). The corrected facts:

- The v1-v3 file is gzip as a whole: it begins with the `0x1f 0x8b` member
  header, and the magic `0x5053474e` ("NGSP" little-endian) is the first
  field of the *decompressed* stream, not of the file. The v4-and-later
  container instead stores the magic in plaintext at file offset 0. Format
  detection therefore branches on the first bytes: gzip signature → v1-v3,
  plaintext magic → v4+ (see §6).
- Header fields are little-endian: magic, version, point count (`uint32`,
  reference maximum `INT32_MAX`), SH degree, fractional bits, flags,
  reserved (one byte each). 16 bytes total for v1-v3.
- Flags: bit `0x1` antialiased (informational), bit `0x2` extension records
  follow the attribute streams.
- Attribute-major ordering: positions, alphas, colours, scales, rotations,
  spherical harmonics.
- Positions: v1 stores float16 (2 bytes per component); v2-v3 store 24-bit
  fixed-point signed integers whose fractional-bit count comes from the
  header rather than being fixed.
- Scales: 8-bit, log-encoded.
- Rotations: v1-v2 store the first three quaternion components (x, y, z) as
  8-bit signed values, 3 bytes per rotation, with the real component
  reconstructed; v3 packs the smallest three components as 10-bit signed
  values plus a 2-bit largest-component index, 4 bytes per rotation. The
  component order and the reconstruction of the dropped component are the
  highest-risk part of the decoder and get dedicated fixtures.
- Alphas: 8-bit unsigned. Colours: 8-bit unsigned RGB carrying the SH DC
  term — the SH stream holds rest coefficients only.
- Spherical harmonics: 8-bit signed, `(degree+1)² − 1` rest coefficients per
  colour channel with the channel as the inner (faster-varying) axis; the
  header field admits degrees 0-4.

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

## 6. Decision — `CanRead()` strategy

**Accepted 2026-07-20, implemented in `SpzReader::CanRead()`: bounded partial
decompression of the container magic, plus the plaintext v4 signature.**

The initial framing of this question ("the magic sits inside the gzip member,
so there is no signature at offset 0") turned out to be incomplete: it is true
for v1-v3, but the v4 layout stores the magic in plaintext at offset 0, which
is also how the reference implementation distinguishes the two generations.
The accepted behavior:

- `.spz` extension, then signature: a plaintext `NGSP` magic at offset 0 is
  claimed immediately — a v4 file must reach `Read()` so it fails with the
  specific unsupported-version diagnostic (`GSPZ-E003`, §3) instead of USD
  reporting that no plugin was found.
- A `0x1f 0x8b` gzip signature alone is *not* enough (that is the over-broad
  direction §7.6 warns about): the reader inflates only the first four
  decompressed bytes — bounded over a 64 KiB file prefix, retrying against
  the whole file when oversized gzip header fields or stored-block padding
  make the prefix inconclusive — and compares them against the magic.
- Header fields beyond the magic (version, count, SH degree) are deliberately
  *not* validated in `CanRead()`: a defective SPZ file is still an SPZ file,
  and §7.6 assigns the explanation to `Read()`'s diagnostics, not to a silent
  routing refusal.

## 7. Open items

- Confirm the SH quantization bit-depth actually present in real v1-v3 assets;
  the packing bit-depth is an encoder option, and the decoder must not assume
  the default.
- Confirm degree-4 handling. The container reader accepts the specification
  range (degrees 0-4) structurally, so the decision is confined to the
  semantic decoder: `GaussianCloudData` covers degrees 0-3, so if real v1-v3
  files use degree 4 the decoder needs an explicit accept-or-reject decision
  rather than a silent truncation.
- Identify a legally redistributable SPZ asset with recorded provenance for the
  corpus, per the release criteria.
