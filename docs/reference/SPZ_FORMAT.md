# SPZ format scope and decisions

This records the specification source, licence review, and scope decisions the
v0.3.0 [release plan](../roadmap/release-plan.md#v030--spz-import) requires
*before* SPZ decoding is implemented. The format-independent model the decoder
targets is [GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md); the
SPZ-specific semantic mapping is [SPZ_MAPPING.md](SPZ_MAPPING.md), mirroring
[PLY_MAPPING.md](PLY_MAPPING.md).

Status: decisions accepted 2026-07-20; container reader implemented
2026-07-20 (`plugins/gaussian-spz/src/io/SpzReader.*`); semantic decoder and
USD integration implemented 2026-07-20
(`plugins/gaussian-spz/src/io/GaussianSpzDecoder.*`, mapping in
[SPZ_MAPPING.md](SPZ_MAPPING.md)).

## 1. Specification source

SPZ is published by Niantic Labs at
<https://github.com/nianticlabs/spz> under the **MIT licence**. The repository
contains both a reference implementation and a prose format description in its
`README`, so the binary layout is **documented, not inferred**. This satisfies
the standing rule that undocumented binary layouts are never guessed
([design policy](../design/DESIGN_POLICY.md) §4: Phase 3 formats are accepted
only once documented).

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
coverage. v4 is tracked for reconsideration after SOG v2 ships
([release plan](../roadmap/release-plan.md)), where it is an additive change
behind the same container-version check rather than a rewrite.

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

SPZ conventionally stores right-up-back (RUB). Since
[ADR 0001](../adr/0001-model-frame-is-rub.md) (v0.4.0), RUB *is* the model's
reference frame ([GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md)
§2), so **the SPZ decoder applies no frame conversion**; the RDF→RUB
Y/Z-negation duty now sits with the PLY decoder. (Through v0.3.0 the model
frame was PLY-native RDF and this decoder carried the inverse conversion.)
Passing one format through unconverted while the other's convention differs
would place equivalent PLY and SPZ assets in different orientations on the
same stage, which the equivalence criterion forbids; the frame handling is
covered by the PLY/SPZ equivalence fixtures, which would otherwise disagree
on sign in two axes.

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
  decompressed bytes — bounded over a 64 KiB file prefix, retrying once over
  a 1 MiB prefix when oversized gzip header fields or stored-block padding
  make the first attempt inconclusive — and compares them against the magic.
  Routing runs against arbitrary files, so `CanRead()` never reads an
  unbounded amount: a file whose magic is not reachable within the retry
  bound is declined. `ReadHeader()` and `Read()`, which run only on files
  already routed here, do fall back to the whole file.
- Header fields beyond the magic (version, count, SH degree) are deliberately
  *not* validated in `CanRead()`: a defective SPZ file is still an SPZ file,
  and §7.6 assigns the explanation to `Read()`'s diagnostics, not to a silent
  routing refusal.

## 7. Open items

- **Resolved.** SH quantization bit-depth is not an assumption the decoder
  makes: the reference dequantization `(byte - 128) / 128` is independent of the
  encoder's packing bit-depth (gzip restores the zeroed low bits), so
  `GaussianSpzDecoder` reads any bit-depth without a per-asset option. See
  [SPZ_MAPPING.md §3](SPZ_MAPPING.md).
- **Resolved.** Degree-4 handling: the decoder rejects degree 4 with the
  *unsupported* diagnostic `GSPZ-E011` (not malformed), and `DecodeMetadata`
  applies the same check. `GaussianCloudData` carries degrees 0-3; degree 4
  is deferred to a release whose shared model carries it. See
  [SPZ_MAPPING.md §7](SPZ_MAPPING.md).
- **Resolved.** A legally redistributable SPZ asset with recorded provenance
  for the corpus: two CC0-1.0 author-captured Scaniverse exports are committed
  under `plugins/gaussian-spz/tests/corpus/`, derived by
  `scripts/spz_subset.py` and checked semantically by the smoke test. See §8
  for what those real exports revealed. (Still open: PLY/SPZ equivalence pairs
  remain in the "Equivalence and real assets" workstream.)
- Decide whether the import pipeline needs an overall decompressed-size policy
  for hostile inputs. Container memory is bounded by DEFLATE's inherent 1032:1
  expansion over the file size (the payload additionally by the declared-count
  plausibility check; extension records have no declared size to check), which
  still lets a small crafted file demand gigabytes. Whether to cap that is a
  host-application policy question, not a container fact — revisit alongside
  the semantic decoder.

## 8. Producer traits observed in real exports

Facts from the two Scaniverse exports admitted to the corpus (see each asset's
`PROVENANCE.md`). They are recorded here because a reader that assumes the
opposite would fail on real files, and none of them is stated by the
specification.

| Trait | Observed | Consequence |
| --- | --- | --- |
| Far-field background shell | 10,242 Gaussians — a 5×-subdivided icosphere, 10·4⁵+2 vertices — at radius ≈240, all at the identical quantized alpha 253 | Environment backdrop, not scene content. It outranks most real content by opacity, so any opacity-ranked subset of such a file must crop by position first; `scripts/spz_subset.py --aabb` exists for this. |
| gzip framing | `FLG` 0 (no `FNAME`/`FCOMMENT`/`FEXTRA`), `MTIME` 0, `OS` byte **19** — outside the range RFC 1952 defines | The reader must not validate the `OS` byte, and must not depend on optional fields being present. |
| Header | v2, SH degree 3, `fractionalBits` 12, `flags` `0x01` (antialiased), `reserved` 0 | The corpus assets are the first real-world trigger of `GSPZ-W002`; before them the antialiased-flag path was fixture-only. |
| Scene scale | content within ≈0.25 units of the origin, with a sparse tail of incidental surroundings several units out | On-device captures are normalized, not metric. Nothing in the container states a unit, so the stage's `metersPerUnit = 1.0` is a convention, not a measurement. |

Two independent captures agreeing on every row is what makes these producer
traits rather than one-off accidents. A second producer's exports should be
compared against this table before its rows are generalized.

SPZ v1-v3 carries no geolocation, capture-time, or device field anywhere in the
container, and the observed framing sets `MTIME` to zero, so a `.spz` file
discloses nothing about where or when it was captured beyond what the geometry
itself depicts.
