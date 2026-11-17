# ADR 0002 — Vendor a decompression-only Zstandard decoder for SPZ v4

- **Status:** Accepted (2026-11-17, maintainer-ratified). The binary-size
  measurement moves to the FMT-07 vendoring change; see *Package change*.
- **Scope:** the `gaussian-spz` bundle, `third_party/`, the bundle and root
  third-party notices, and the import limits for SPZ v4
- **Context documents:** the
  [FMT-05 spike report](../reports/format-spike-spz-v4-2026-11-17.md),
  [SPZ_FORMAT.md](../reference/SPZ_FORMAT.md), and
  [IMPORT_LIMITS.md](../reference/IMPORT_LIMITS.md)

## Context

SPZ version 4 stores each attribute class as its own Zstandard (zstd) frame
behind a plaintext header and a table of contents (spike §2). Every v4 stream
is byte-identical to the matching v3 attribute section (spike §3). The only
new capability the importer needs is zstd decompression. Neither OpenUSD nor
the OpenStrata runtime gives a plugin a zstd decoder it can rely on, so the
choice is between vendoring one and not reading v4.

The repository already vendors three decoders on the same pattern: tinyPLY,
miniz (raw DEFLATE for SPZ v1-v3, ZIP reading for SOG), and the libwebp
decoder subset for SOG. The libwebp pattern is the model here: an immutable
upstream release, a decoder-only subset chosen by directory and build
definitions, no edits to upstream sources, and a `VENDORING.md` that records
what was kept and dropped.

## Decision

1. **Vendor Zstandard v1.5.7, decompression only, under
   `third_party/zstd/`.** Use the upstream release archive
   `zstd-1.5.7.tar.gz` from the `v1.5.7` GitHub release (commit
   `f8745da6ff1a`, 2025-02-18, the latest release). Its SHA-256 is
   `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`.
   This matches the `.sha256` file upstream publishes with the release, and
   was checked on 2026-11-17.
2. **Take the BSD-3-Clause licence.** zstd is dual-licensed (BSD-3-Clause or
   GPLv2, at the licensee's choice). The project takes the BSD-3-Clause
   option, as it does for libwebp. Vendor upstream `LICENSE`. Do not vendor
   `COPYING` (GPLv2): the project does not distribute zstd under it.
   Record the choice in the notices.
3. **Keep upstream's decoder subset and edit nothing.** The kept files match
   upstream's own single-file decoder recipe
   (`build/single_file_libs/zstddeclib-in.c`):
   - `lib/common/`: `debug.c`, `entropy_common.c`, `error_private.c`,
     `fse_decompress.c`, `zstd_common.c`, `xxhash.c`, and the headers they
     include;
   - `lib/decompress/`: `huf_decompress.c`, `zstd_ddict.c`,
     `zstd_decompress.c`, `zstd_decompress_block.c`, and their headers;
   - `lib/zstd.h`, `lib/zstd_errors.h`.

   Dropped: `lib/compress/`, `lib/dictBuilder/`, `lib/legacy/`,
   `lib/deprecated/`, `lib/dll/`, `common/pool.*`, `common/threading.*`,
   `decompress/huf_decompress_amd64.S`, and the upstream build files. The
   decoder needs no compressor, dictionaries, pre-v0.8 frame formats, or
   threads.
4. **Build it with fixed definitions** in a C translation-unit list inside
   `gaussianSpzDecoder`, as miniz is built today:

   | Definition | Why |
   | --- | --- |
   | `ZSTD_LEGACY_SUPPORT=0` | No pre-v0.8 frames. The legacy decoders are not vendored. |
   | `ZSTD_DISABLE_ASM=1` | The one assembly file is only an x86-64 speed-up. One C build serves MSVC, Clang, and GCC. |
   | `ZSTD_TRACE=0`, `DEBUGLEVEL=0` | No tracing hooks, no debug output |
   | `ZSTD_STRIP_ERROR_STRINGS` | The reader maps error *codes* to its own `GSPZ-` diagnostics. The strings are dead weight. |
   | `XXH_NAMESPACE=ZSTD_` | Prefixes the bundled xxHash so it cannot clash with another xxHash in the same image |
   | `ZSTDLIB_VISIBLE=`, `ZSTDERRORLIB_VISIBLE=` | See item 5 |

   The same definitions appear in upstream's `zstddeclib-in.c`, except the
   two visibility overrides. Upstream also leaves `DYNAMIC_BMI2` alone, and so
   does this build. On GCC and Clang for x86-64 the decoder then selects a
   BMI2 code path at run time with `cpuid`. MSVC and arm64 builds have only
   the portable path. Both paths produce the same output, so the
   reproducible-package gate is unaffected.
5. **No zstd symbol leaves the plugin library.** On ELF and Mach-O,
   `zstd.h` forces default visibility on its API unless `ZSTDLIB_VISIBLE` is
   set. The `gaussian-spz` targets do not set a hidden visibility preset
   today. A plain vendoring would therefore export `ZSTD_*` and the internal
   `HUF_*` and `FSE_*` functions from `libGaussianSpzFileFormat`. A USD host
   commonly has another libzstd loaded, through OpenVDB, Blosc, TIFF, or the
   DCC itself. On Linux, ELF symbol interposition could then bind the plugin's
   calls to that other zstd version, mixing internal structures across
   versions. The zstd sources are therefore compiled with
   `C_VISIBILITY_PRESET hidden` and both visibility macros empty. The FMT-07
   build check asserts that the shared library exports no symbol matching
   `ZSTD_|HUF_|FSE_|ERR_|XXH`. On Windows nothing is exported without
   `ZSTD_DLL_EXPORT`, which is never set.
6. **The reader decodes each stream in one shot into an exact-size buffer.**
   It never uses the streaming API. The order of checks is:
   1. The source size is within the SPZ source limit. The header passes the
      existing count, SH-degree, and `INT32_MAX` checks, and the shared
      Gaussian ceiling.
   2. `tocByteOffset` is at least 32 and within the file. The extension
      region `[32, tocByteOffset)` is within the existing extension-record
      limit. The TOC fits and exactly fills the rest of the file.
      `numStreams` equals the stream count implied by `shDegree`.
   3. Each TOC `uncompressedSize` equals the size computed from `numPoints`
      and `shDegree` with checked arithmetic. Allocation happens only after
      this check, so the decoder never sees an attacker-chosen output size.
   4. For each stream, `ZSTD_findFrameCompressedSize` locates exactly one
      data frame at the start of the stream. Its recorded content size must
      equal the expected size when it is present. In every stream except the
      last, the data frame must fill the TOC `compressedSize`. In the last
      stream, any remaining bytes must be complete zstd skippable frames
      (`ZSTD_isSkippableFrame`). They are not read, and their presence
      reports `GSPZ-W001` as other ignored extensions do.
   5. `ZSTD_decompressDCtx` decodes only that one data frame into the
      exact-size buffer, and the result must equal the buffer size. One
      `ZSTD_DCtx` is created per `Read()` and reused for every stream. Frame
      checksums, when present, stay verified (the upstream default).

   Bounding the frame before decoding matters because `ZSTD_decompressDCtx`
   decodes concatenated frames and skips skippable frames on its own. Without
   step 4, extra frames would be accepted silently.
7. **Diagnostics.** v4 container failures use the existing SPZ classes
   rather than new codes: structural errors (TOC, stream count, sizes,
   offsets, frame layout) are `GSPZ-E004`, a stream that ends early is
   `GSPZ-E007`, and a zstd decode error or a size mismatch after decoding is
   `GSPZ-E008`. Their catalogue text is broadened from gzip/DEFLATE terms
   to cover both generations. `GSPZ-E003` keeps its meaning for versions
   above 4. The coordinate-system rejection proposed in spike §8 needs a new
   *unsupported* code and is decided in FMT-07, not here.

## Bounds on hostile input

- **Output size.** A one-shot decode cannot write beyond its output buffer;
  zstd returns `dstSize_tooSmall` instead. The buffer size comes from the
  header after the shared count ceiling. At 8,000,000 Gaussians and SH
  degree 3 that is 65 bytes per Gaussian, or 520 MB across all streams.
  This matches the bound on the attribute data in the v1-v3 path. A
  high-ratio "zip bomb" stream can only fail.
- **Window size.** The streaming-only limit `ZSTD_d_windowLogMax` is *not*
  part of the bound, contrary to the suggestion in spike §7. In
  `zstd_decompress.c` v1.5.7 it is checked only on the streaming path
  (`ZSTD_decompressStream`). A one-shot decode uses the caller's output
  buffer as its window and allocates no window buffer of its own. Setting it
  would have no effect.
- **Working memory.** Besides the output buffers, decoding uses one
  `ZSTD_DCtx`, whose size is fixed by the build and independent of the input.
  The reader never decodes into a temporary larger than the final array.
- **Input size.** The 1 GiB SPZ source limit and the 64 MiB extension-record
  limit, today labelled "SPZ v1-v3", are extended to v4 unchanged.
- **Time.** zstd decoding is linear in input plus output size, so the output
  bound also bounds the time.
- **Failure atomicity.** A failed stream fails the whole import with no
  partial `GaussianCloudData` or layer, as for every other reader.
- **Fixture coverage (FMT-07).** Negative fixtures for each rule in item 6:
  a short and an overlong TOC, a wrong stream count, a size that disagrees
  with the header, a truncated frame, a corrupted frame, a second data frame
  inside a stream, a skippable frame outside the last stream, and a frame
  whose content size disagrees with the TOC. These go beside the existing
  `oversized-*` limit fixtures.

## Package change

- **No new plugin, extension, registration, or runtime dependency.** The
  decoder is linked statically into the existing `libGaussianSpzFileFormat`
  on every release target. `dependencies.json` and the activation files do
  not change.
- **Notices.** `THIRD_PARTY_NOTICES.md` (root) and
  `plugins/gaussian-spz/THIRD_PARTY_NOTICES.md` (shipped in the package)
  gain a zstd entry. Because BSD-3-Clause requires a binary redistribution
  to reproduce the copyright notice, conditions, and disclaimer, the
  *packaged* notice carries the full zstd licence text, not a pointer to the
  source repository.
- **Binary size: measured in FMT-07 CI.** The spike could not run
  upstream's build. A 2026-11-17 attempt to compile the subset locally, in a
  scratch harness, was also blocked by the session's permission policy. The
  maintainer accepted this ADR without the figure, on the condition that the
  FMT-07 vendoring change records it from CI for every release target:
  Windows x86-64 (MSVC 143), Ubuntu 24.04 x86-64 (GCC), and macOS 15 arm64
  (Apple Clang). The figure is the size of `libGaussianSpzFileFormat`
  before and after, in the Release configuration the release workflow uses.
  It is recorded in the FMT-07 pull request and in
  `third_party/zstd/VENDORING.md`. For reference, the locally built v0.10.0
  Windows library is 226,304 bytes.

## Consequences

- SPZ v4 can be read with no change outside the `gaussian-spz` bundle and
  `third_party/`.
- `third_party/zstd/VENDORING.md` records the archive, its SHA-256, the
  kept and dropped files, the definitions, and the re-vendoring steps. A zstd
  security release is re-vendored the same way as libwebp. Re-vendoring is
  required for any upstream advisory that affects the decompression path.
- **The existing notices fall short of the same rule.** While writing this
  ADR it was found that the packaged `THIRD_PARTY_NOTICES.md` of
  `gaussian-spz` (miniz, MIT) and of `gaussian-sog` (miniz, MIT; libwebp,
  BSD-3-Clause and its patent grant) refer to the source repository for the
  licence texts instead of including them. Both licences require the text
  to travel with the binary. This is fixed in v0.11.0 alongside the zstd
  notice (FMT-08), and the package consumer gate is extended to check that
  each packaged notice contains every vendored licence text.

## Alternatives considered

- **Do not support SPZ v4.** Rejected: the reference library, its web
  converter, and SplatTransform write v4 by default (spike §4), so the
  `GSPZ-E003` path would become the common case for new files.
- **Link a system or runtime-provided libzstd.** Rejected: neither the
  OpenStrata runtime nor OpenUSD promises one on every target, and binding
  to whatever zstd the host happens to load is the interposition problem in
  item 5, made permanent.
- **Vendor upstream's generated `zstddeclib.c` single file.** It would be
  one translation unit, but it is generated by a script and is not in the
  release archive. Vendoring the original files keeps the copy a byte-exact
  subset of the signed release. It also allows a mechanical re-vendor, with
  no generation step to trust.
- **Write a zstd decoder in this repository.** Rejected: a frame decoder
  with Huffman and FSE tables is a large attack surface to own, and upstream
  is fuzzed continuously (OSS-Fuzz). The vendoring rule exists to avoid
  exactly this.
- **Vendor the full library, compressor included.** Rejected: it more than
  doubles the vendored code for no import path. Test fixtures are generated
  by development-only tooling (spike §6), not by the plugin.
