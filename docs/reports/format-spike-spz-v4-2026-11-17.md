# Format spike: SPZ version 4 — 2026-11-17

This is the FMT-05 evaluation in [current work](../roadmap/current.md).
It records what was observed on 2026-11-17 against the named upstream
revisions and files. It is not a support claim. The
[capability matrix](../reference/CAPABILITY_MATRIX.md) still lists SPZ v4 as
unsupported (`GSPZ-E003`).

## Summary

| Check | Result |
| --- | --- |
| Specification | Documented in prose by the MIT reference repository, with a reference implementation that agrees with it on every fact in §2. |
| Relation to v1-v3 | Only the container changes. Each v4 stream is byte-identical to the matching v3 attribute section, measured on reference-written files (§3). The existing v3 decoder semantics apply unchanged. |
| Writer defaults | The reference library, its web converter, and SplatTransform since 2.1.0 write v4 by default. Postshot supports v4 since 1.1.69; its default is not documented. Scaniverse v4 export was announced but not confirmed (§4). |
| Coordinate frame | The spec default is RUB, as before. Two new risks: an extension can declare another frame, and SplatTransform since 3.0.0 writes PLY-frame data without declaring it (§5). |
| Fixtures | No redistributable real-world v4 capture was found. Deterministic fixtures can be generated in-repo, and an MIT reference-written file serves as an independent cross-check (§6). |
| Dependency cost | One new vendored decoder: Zstandard v1.5.7, BSD-3-Clause, decompression subset only. Needs the FMT-06 ADR (§7). |
| Recommendation | Keep SPZ v4 as the v0.11.0 target. No blocker was found. Scope as in §8. |

## 1. Sources

| Source | Revision | Licence |
| --- | --- | --- |
| [nianticlabs/spz](https://github.com/nianticlabs/spz): `README.md`, `extensions/README.md`, `src/cc/load-spz.cc`, `src/cc/zstd-batch.cc`, `src/cc/splat-types.h` | `main` on 2026-11-17, commit `d5abdee9b37c` (2026-11-13). Format v4 first ships in library tag `v3.0.0` (2026-05-04). | MIT |
| [SplatTransform](https://github.com/playcanvas/splat-transform): `src/lib/writers/write-spz.ts`, `src/lib/spz-module.ts`, `src/lib/readers/read-spz.ts`, `src/cli/index.ts`, `test/fixtures/` | tag `v3.11.2`, commit `6f642939dd5d` (equal to `main` on 2026-11-17). Its SPZ backend is `@adobe/spz` 0.3.1, a WebAssembly build of the reference library. | MIT |
| [Zstandard](https://github.com/facebook/zstd) | tag `v1.5.7`, commit `f8745da6ff1a` (2025-02-18), the latest release | BSD-3-Clause or GPLv2, at the user's choice |
| [Niantic Spatial, "SPZ 4 is here"](https://www.nianticspatial.com/blog/spz4) | published 2026-05-05 | — |
| [Postshot v1.1.69 release notes](https://www.jawset.com/docs/d/Postshot+User+Guide/Release+Notes/v1.1.69) | dated 2026-08-18 | — |

The layout is documented, not inferred, which meets the
[format selection policy](../design/DESIGN_POLICY.md#4-format-selection).
The upstream README now calls v1-v3 "read-only" legacy formats: the
reference library writes v4 unless asked otherwise.

## 2. Container layout

All fields are little-endian.

```text
bytes 0..31           NgspFileHeader (plaintext)
bytes 32..tbo-1       extension records, only when flags & 0x2
bytes tbo..tbo+16N-1  TOC: N × { u64 compressedSize, u64 uncompressedSize }
bytes tbo+16N..end    N zstd streams, back to back
```

| Header field | Type | Notes |
| --- | --- | --- |
| `magic` | u32 | `0x5053474e`, the bytes `NGSP` at offset 0 |
| `version` | u32 | 4 |
| `numPoints` | u32 | The reference rejects 0, values above `INT32_MAX`, and values above `fileSize × 1024 / 9` |
| `shDegree` | u8 | 0-4 |
| `fractionalBits` | u8 | As in v2-v3 |
| `flags` | u8 | `0x1` antialiased, `0x2` extension records present |
| `numStreams` | u8 | Number of TOC entries |
| `tocByteOffset` | u32 | `tbo`. 32 when there are no extension records |
| `reserved` | u8[12] | Must be zero. The reference does not check it. |

- The streams are, in order, positions, alphas, colours, scales, rotations,
  and spherical harmonics. Empty streams are omitted, so `numStreams` is 5 at
  SH degree 0 and 6 otherwise.
- The reference reader rejects a file unless the TOC entries exactly fill
  the rest of the file, `numStreams` equals the expected stream count, and
  each `uncompressedSize` equals the stream size implied by `numPoints` and
  `shDegree`. It decompresses each stream into an exact-size buffer.
- Each stream is one zstd frame. The last stream may also be followed by
  zstd *skippable* frames (magic `0x184D2A53`) that carry extension data.
  Its `compressedSize` covers them; its `uncompressedSize` does not. A
  standard zstd decoder skips such frames, so a reader that ignores
  extensions still sees exactly the core data.
- Extension records are `[u32 type][u32 byteLength][payload]`, repeated
  over `[32, tbo)`. Unknown types are skipped by length. v1-v3 files carry
  the same records after the attribute streams, and since commit `d5abdee`
  the reference reads them back from v1-v3 too.

Detection is unchanged from
[SPZ_FORMAT.md §6](../reference/SPZ_FORMAT.md#6-decision--canread-strategy):
`NGSP` at offset 0 means v4, and the gzip signature means v1-v3. The current
`CanRead()` already claims both.

## 3. Relation to v1-v3

The upstream SplatTransform test fixtures `minimal-v3.spz` and
`minimal-v4.spz` were written from the same 4-point cloud by `@adobe/spz`
0.2.1 (`test/fixtures/create-fixtures.mjs`, commit `7112e83`). An independent
Python parser (standard `gzip`, `zstandard` 0.25.0) decoded both files per
§2 and compared them:

| Check | Result |
| --- | --- |
| v4 header | version 4, 4 points, SH degree 0, `fractionalBits` 12, flags 0, `numStreams` 5, `tocByteOffset` 32, reserved bytes zero |
| TOC | Fills the file exactly (220 bytes) |
| Frames | One frame per stream, content size recorded in the frame header, no checksum, window no larger than the content |
| Positions, alphas, colours, scales, rotations | Each v4 stream is byte-identical to the matching v3 attribute section |

The per-attribute encodings are therefore the v3 encodings: 24-bit fixed-point
positions, smallest-three rotations, and the existing
[SPZ_MAPPING.md](../reference/SPZ_MAPPING.md) dequantization. A v4 reader
only needs to deliver the same six byte spans to `GaussianSpzDecoder`.

SHA-256 of the files examined: `minimal-v4.spz`
`4629adb9f96c4fc6cdc26be5c0fd76bdbf473f47694834dd9e601d34a23ca463`,
`minimal-v3.spz`
`3b2a493c12a625b05ad80d1767c4f59d25087750d8574989873be4ad85493704`.

## 4. Writer defaults

| Producer | v4 status | Evidence |
| --- | --- | --- |
| Reference library (`nianticlabs/spz`) | Default since library `v3.0.0` (2026-05-04) | `LATEST_SPZ_HEADER_VERSION = 4` and `PackOptions::version` defaults to it. The blog says "writes now producing v4 by default". |
| Niantic web converter | v4 only | The blog says the tool "exports files in the new SPZ v4 format". |
| SplatTransform | Default since `v2.1.0` (2026-05-08); `--spz-version 3` remains | `src/cli/index.ts`, `write-spz.ts` |
| Postshot | Supported since 1.1.69 (2026-08-18) | The release notes say "Support SPZ format version 4". They do not say whether v4 is the default. |
| Scaniverse | Announced | The blog says v4 exports are coming "within a few months". No release confirming it was found. |
| Adobe tools | Not established | Adobe co-designed the extension system, but no source says which version its tools write. |

The revisit trigger in the
[SPZ container scope](../reference/SPZ_FORMAT.md#3-container-version-scope)
was a producer that writes v4 by default. Two widely used producers now do.

## 5. Coordinate frame

The default frame is still RUB, which is the model frame
([ADR 0001](../adr/0001-model-frame-is-rub.md)). FMT-05 found two ways a
file can differ from that default.

**Declared frame.** The `SPZ_ADOBE_coordinate_system` extension (`0xADBE0003`,
one u32) labels the frame the data is stored in. It names one of 16 systems:
eight axis-sign variants of RUB (for example RDF = 6), and eight
"rotated-family" variants that also swap Y and Z. The reference loader
converts from the declared frame to the requested one. A rotated-family
conversion also rotates SH coefficients with a Wigner D-matrix. The upstream
documentation warns that a reader ignoring this extension "will silently
misinterpret the data". The record can appear in v1-v3 files as well as v4.

Today the importer treats every extension record as opaque. It reports
`GSPZ-W001` and authors the data as RUB. A file that declares a non-RUB frame
is therefore imported in the wrong orientation, with only a warning. This
affects the v1-v3 path that already ships, not only v4.

**Undeclared frame.** SplatTransform since `v3.0.0` (2026-07-09, commit
`13c93a4`) writes SPZ with `from: UNSPECIFIED` and no coordinate extension,
after converting the data to PLY's RDF frame. Its own comment says this is so
that "reader and writer therefore share one convention". Between `v2.1.0`
and `v3.0.0` it wrote `from: RDF`, which stores spec-conforming RUB data.
A conforming reader opens a SplatTransform 3.x file with Y and Z negated
relative to the source PLY. This was read from source code, not measured on a
generated file, because installing the npm package was not permitted in the
spike session. The file itself contains nothing to tell the two conventions
apart.

## 6. Fixtures

- **Real assets.** The upstream `samples/*.spz` are v1-v3 gzip files from
  2024. No v4 capture with a redistribution licence was found.
- **Reference-written.** `minimal-v4.spz` from SplatTransform (MIT) is
  written by the reference library and is useful as an independent decode
  check. It has only 4 points and SH degree 0.
- **Derived from the CC0 corpus.** The committed `yashica-t4` and
  `leica-sofort` PLYs can be converted to v4 by the reference library with
  `from: RDF`. This makes spec-conforming RUB files with recorded
  provenance, as the compressed PLY corpus does. Converting the committed
  Scaniverse v2 SPZ files is less suitable: v2 stores first-three rotations,
  so moving to v4 re-quantizes every rotation.
- **Deterministic.** `plugins/gaussian-spz/tools/generate_fixtures.py`
  already produces the v3 attribute bytes. A v4 writer only adds the header,
  the TOC, and one zstd frame per stream, so the equivalence fixtures can
  gain v4 partners for their v3 cases. Generating them needs the Python
  `zstandard` package as a development-only tool dependency.

## 7. Dependency and package cost

Zstandard v1.5.7 is the decoder candidate for FMT-06.

- **Licence.** BSD-3-Clause, choosing it over GPLv2. This matches the
  libwebp entry in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
- **Subset.** `lib/common/` and `lib/decompress/` plus `lib/zstd.h` and
  `lib/zstd_errors.h`. That is about 964 KB of source, of which `xxhash.h`
  is 262 KB and `zstd.h` 182 KB. The compressor, dictionary builder,
  legacy-format decoders, and multithreading pool are not needed. Upstream's
  own `zstddeclib` amalgamation selects the same subset with
  `ZSTD_LEGACY_SUPPORT=0`, `ZSTD_STRIP_ERROR_STRINGS`, and
  `ZSTD_DISABLE_ASM=1`. Trimming by directory and build definitions, with no
  source edits, follows the libwebp vendoring rule.
- **Assembly.** `huf_decompress_amd64.S` is only an optimization. Defining
  `ZSTD_DISABLE_ASM` keeps one C build on every release target, including
  MSVC.
- **Binary size.** Not measured. Upstream's amalgamation script was not run
  because the spike session did not allow running downloaded scripts. FMT-06
  measures the size on the release targets.
- **Bounds on hostile streams.** The container gives an exact output size for
  every stream. The reader can decode into an exact-size buffer and fail if
  the stream ends early or produces more, cap the window with
  `ZSTD_d_windowLogMax`, and accept skippable frames only after the first
  frame of the last stream. The existing
  [import limits](../reference/IMPORT_LIMITS.md) still bound the declared
  sizes before any allocation.

## 8. Proposed scope for FMT-06 to FMT-08

- **In scope.** v4 with SH degrees 0-3. A full read and a metadata-only read
  from the plaintext header alone. Extension records parsed by type and
  length in both generations.
- **Coordinate extension.** A declared frame of RUB is accepted. Any other
  declared frame fails with a new *unsupported* diagnostic instead of the
  current silent `GSPZ-W001`. The same rule applies to v1-v3. Supporting
  the axis-sign family later only needs sign flips like the PLY path's;
  the rotated family needs SH rotation and stays out of scope.
- **Other extensions.** Safe-orbit camera, georeference, and custom attributes
  stay ignored with `GSPZ-W001`. None of them changes the core data.
- **Rejected with specific diagnostics.** A TOC that does not fill the file,
  an unexpected stream count, a stream size that disagrees with the header,
  a zstd frame that fails or yields the wrong size, and a `tocByteOffset`
  below 32 or past the end of the file. Non-zero reserved bytes need a
  decision: the reference accepts them. `GSPZ-E003` keeps its meaning for
  versions above 4.
- **Open for FMT-07.** Whether the importer should warn about SplatTransform
  3.x files, given that nothing in the file identifies them. Recording the
  convention in the mapping reference and reporting it upstream may be the
  right response. Before writing the user-facing text, confirm the frame on a
  generated file.
- **Package impact.** The decoder is compiled into the existing
  `gaussian-spz` binary. No new plugin, extension, or registration is added.

## 9. Not verified in this spike

- No real-world v4 file from Postshot, Scaniverse, or an Adobe tool was
  examined.
- SplatTransform 3.x frame behaviour is based on source code, not on a
  generated file (§5).
- The decoder's binary size on release targets was not measured (§7).
