# SPZ format scope and decisions

This records the format specification and container decisions for the SPZ
importer. See the [v0.3.0 release record](../releases/v0.3.0.md) for initial
delivery, the [capability matrix](CAPABILITY_MATRIX.md) for current support,
and [SPZ_MAPPING.md](SPZ_MAPPING.md) for the semantic mapping.

## 1. Specification source

SPZ is published by Niantic Labs at
<https://github.com/nianticlabs/spz> under the **MIT licence**. The repository
contains both a reference implementation and a prose format description in its
`README`, so the binary layout is **documented, not inferred**. This satisfies
the standing rule that undocumented binary layouts are never guessed
([format selection policy](../design/DESIGN_POLICY.md#4-format-selection)).

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

## 3. Container version scope

**The importer reads SPZ versions 1, 2, 3, and 4.** Versions 1-3 have been
read since v0.3.0. Version 4 is read since v0.11.0, after the
[FMT-05 spike](../reports/format-spike-spz-v4-2026-11-17.md) found that the
reference library, its web converter, and SplatTransform write it by default,
and [ADR 0002](../adr/0002-vendored-zstd-decoder.md) accepted the decoder
dependency.

The two generations differ structurally:

| | v1-v3 | v4 |
| --- | --- | --- |
| Header | 16 bytes, inside the gzip member | 32 bytes, plaintext |
| Compression | gzip, single stream | Zstandard, one frame per attribute stream |
| Layout | attribute-major within one stream | extension zone, table of contents, N streams |
| Extension records | after the attribute streams, inside the gzip member | plaintext zone between the header and the TOC |
| Decoder | vendored miniz (raw DEFLATE) | vendored Zstandard v1.5.7, decompression only |

Each v4 stream is byte-identical to the matching v3 attribute section (spike
§3), so the reader delivers the same six byte spans to the semantic decoder
for both generations. Only the container code differs.

**A version this release does not know fails with a specific
unsupported-version diagnostic (`GSPZ-E003`), not a malformed-container
one.** The distinction is a release criterion: a user handed a newer file
needs to be told the version is not supported yet, not that their file is
corrupt. Version 0 and versions above 4 are `GSPZ-E003`. A layout that no
version defines is malformed (`GSPZ-E004`): a plaintext header with version
1-3, or a gzip member whose header says version 4. The reference reader
accepts the second; no writer produces it, and the format defines version 4
only as the plaintext container.

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

### Version 4 container

Recorded from the upstream README and `load-spz.cc` at commit `d5abdee9b37c`
(spike §2), and confirmed against the reference-written file in
`plugins/gaussian-spz/tests/reference/`. All fields are little-endian.

```text
bytes 0..31           header (plaintext)
bytes 32..tbo-1       extension records, only when flags & 0x2
bytes tbo..tbo+16N-1  TOC: N × { u64 compressedSize, u64 uncompressedSize }
bytes tbo+16N..end    N zstd streams, back to back
```

- Header: magic, version (4), point count, SH degree, fractional bits, flags
  (one byte each from offset 12), then `numStreams` (u8), `tocByteOffset`
  (u32), and twelve reserved bytes.
- Streams: positions, alphas, colours, scales, rotations, spherical
  harmonics, with empty streams omitted, so `numStreams` is 5 at SH degree 0
  and 6 otherwise. Attribute widths are v3's: 24-bit fixed-point positions,
  smallest-three rotations.
- Each stream is one zstd frame. The last stream may be followed by zstd
  skippable frames that carry extension data; its `compressedSize` covers
  them, its `uncompressedSize` does not.

The reader checks, in this order (ADR 0002 item 6), and allocates the
attribute buffer only after every check before decoding has passed:

| Check | Failure |
| --- | --- |
| The file holds the 32-byte header | `GSPZ-E007` |
| Count, SH degree, and the shared Gaussian ceiling, as in v1-v3 | `GSPZ-E002`, `E005`, `E006`, `E015` |
| `numPoints × 9` is at most 1024 times the file size, the reference reader's bound | `GSPZ-E007` |
| `tocByteOffset` is at least 32 and within the file | `GSPZ-E004` |
| An extension zone is present only with the `0x2` flag, and is at most 64 MiB | `GSPZ-E009`, `E015` |
| `numStreams` matches the SH degree | `GSPZ-E004` |
| The TOC fits in the file | `GSPZ-E007` |
| A coordinate-system record declares RUB (§5) | `GSPZ-E016`, `E004` |
| Each `uncompressedSize` equals the size the header implies | `GSPZ-E004` |
| The streams fit in the file (`E007`) and fill it exactly (`E004`) | `GSPZ-E007`, `E004` |
| Each stream begins with one data frame, not a skippable frame | `GSPZ-E004` |
| The frame is complete (`E007`) and its header decodable (`E008`) | `GSPZ-E007`, `E008` |
| A recorded frame content size equals the TOC size | `GSPZ-E004` |
| Without a recorded size, the frame's block bound can hold the TOC size | `GSPZ-E007` |
| The data frame fills every stream but the last | `GSPZ-E004` |
| Only complete skippable frames follow it in the last stream | `GSPZ-E004`, `E007` |
| Each frame decodes, passes its checksum when it has one, and yields exactly the TOC size | `GSPZ-E008` |

Two deliberate choices follow the reference reader. Non-zero reserved bytes
are accepted, as the reference does not check them and the v1-v3 reserved
byte is not checked either. Skippable frames are bounded and counted but
never decompressed; they are reported with the ignored extensions
(`GSPZ-W001`). The metadata-only read (`ReadHeader`) runs every check up to
the frames, because v4 keeps that structure in plaintext.

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

### Declared frame

**Decided in FMT-07 (v0.11.0): a file that declares a storage frame other
than RUB is refused with the unsupported diagnostic `GSPZ-E016`, in every
container version.**

The `SPZ_ADOBE_coordinate_system` extension record (type `0xADBE0003`, one
u32 value) names the frame the stored data is in. The reference loader
converts from that frame on read, and its documentation warns that a reader
ignoring the record "will silently misinterpret the data". Through v0.10.0
the importer treated every extension record as opaque, so a v1-v3 file that
declared, for example, RDF was authored in the wrong orientation with only
`GSPZ-W001`.

| Declared value | Result |
| --- | --- |
| 0 (UNSPECIFIED) or 4 (RUB) | Imported as before; the record is honored, not reported as ignored |
| 1-3, 5-16 (any other defined frame) | `GSPZ-E016` |
| above 16 (undefined) | `GSPZ-E016` |
| a record shorter than four payload bytes | `GSPZ-E004` |

Every coordinate-system record in the file must declare RUB; there is no
first-one-wins rule. Extension records are walked as the reference reader
walks them: `[u32 type][u32 byteLength][payload]`, stopping without an error
at the first record whose length runs past the end. In v1-v3 the records
follow the attribute streams, so only the full read sees them; in v4 the
metadata-only read refuses the file too.

Converting instead of refusing is a later option. The eight axis-sign frames
need only the sign flips the PLY decoder already applies, but the eight
rotated frames also rotate spherical harmonics, which is out of scope.

### Undeclared frame (SplatTransform 3.x)

SplatTransform since 3.0.0 (2026-07-09) converts data to PLY's RDF frame and
writes it with no coordinate-system record (spike §5). Nothing in such a file
distinguishes it from a conforming RUB file, so the importer cannot detect
it: it is imported as RUB, which negates Y and Z relative to the PLY the file
was converted from. A conforming reader, including the reference library,
does the same. SplatTransform 2.1.0 to 2.x wrote spec-conforming RUB. This
was read from SplatTransform's source code and has not been confirmed on a
generated file. No warning is emitted, because any warning would also fire on
every conforming file.

## 6. Decision — `CanRead()` strategy

**Accepted 2026-07-20, implemented in `SpzReader::CanRead()`: bounded partial
decompression of the container magic, plus the plaintext v4 signature.**

The initial framing of this question ("the magic sits inside the gzip member,
so there is no signature at offset 0") turned out to be incomplete: it is true
for v1-v3, but the v4 layout stores the magic in plaintext at offset 0, which
is also how the reference implementation distinguishes the two generations.
The accepted behavior:

- `.spz` extension, then signature: a plaintext `NGSP` magic at offset 0 is
  claimed immediately, whatever its version. A v4 file is read; a file with
  another version must still reach `Read()` so it fails with its specific
  diagnostic (`GSPZ-E003` or `GSPZ-E004`, §3) instead of USD reporting that
  no plugin was found.
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

## 7. Validation decisions

- SH quantization bit-depth is not an assumption the decoder
  makes: the reference dequantization `(byte - 128) / 128` is independent of the
  encoder's packing bit-depth (gzip restores the zeroed low bits), so
  `GaussianSpzDecoder` reads any bit-depth without a per-asset option. See
  [SPZ_MAPPING.md §3](SPZ_MAPPING.md).
- Degree-4 handling: the decoder rejects degree 4 with the
  *unsupported* diagnostic `GSPZ-E011` (not malformed), and `DecodeMetadata`
  applies the same check. `GaussianCloudData` carries degrees 0-3. See
  [SPZ_MAPPING.md §7](SPZ_MAPPING.md).
- A legally redistributable SPZ asset with recorded provenance
  for the corpus: two CC0-1.0 author-captured Scaniverse exports are committed
  under `plugins/gaussian-spz/tests/corpus/`, derived by
  `scripts/spz_subset.py` and checked semantically by the smoke test. See §8
  for what those real exports revealed.

- Cross-format equivalence is exercised by
  [deterministic fixtures](EQUIVALENCE.md). The enforced compressed and
  decompressed input budgets are in [IMPORT_LIMITS.md](IMPORT_LIMITS.md).
- Version 4 has no redistributable real-world capture yet (spike §6). Its
  coverage is the deterministic fixtures from
  `plugins/gaussian-spz/tools/generate_fixtures.py`, v4 partners in the
  PLY/SPZ equivalence suite, and an independent check: SplatTransform's test
  files, written by the reference library from one 4-point cloud as v3 and
  as v4, must decode to identical clouds
  (`plugins/gaussian-spz/tests/reference/`).

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

The core SPZ header carries no geolocation, capture-time, or device field, and
the observed framing sets `MTIME` to zero, so these exports disclose nothing
about where or when they were captured beyond what the geometry itself
depicts. Newer files can carry an `SPZ_NIANTIC_georeference` extension record;
the importer does not read it and reports it with the other ignored extension
records (`GSPZ-W001`).
