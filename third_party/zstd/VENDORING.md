# Vendored Zstandard (decompression subset)

Provenance and the exact keep/drop record for the vendored copy. The consuming
build wiring lives in `plugins/gaussian-spz`; the decision, its bounds on
hostile input, and the alternatives are in
[ADR 0002](../../docs/adr/0002-vendored-zstd-decoder.md), and the licence is
recorded in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

## Source

- Upstream: <https://github.com/facebook/zstd>
- Release: `v1.5.7` (commit `f8745da6ff1a`, 2025-02-18)
- Archive: `zstd-1.5.7.tar.gz` from the GitHub release, SHA-256
  `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`
  (checked against the archive on 2026-11-17)
- Licence: BSD-3-Clause (`LICENSE`), chosen from the BSD-3-Clause or GPLv2
  dual licence. `COPYING` (GPLv2) is not vendored.

## What is vendored

The decoder files of upstream's own single-file decoder recipe
(`build/single_file_libs/zstddeclib-in.c`), kept as separate files at their
upstream paths under `lib/`. Each file is byte-identical to the release
archive.

- `lib/zstd.h`, `lib/zstd_errors.h`
- `lib/common/`: `debug.c`, `entropy_common.c`, `error_private.c`,
  `fse_decompress.c`, `xxhash.c`, `zstd_common.c`, and the headers they
  include: `allocations.h`, `bits.h`, `bitstream.h`, `compiler.h`, `cpu.h`,
  `debug.h`, `error_private.h`, `fse.h`, `huf.h`, `mem.h`,
  `portability_macros.h`, `xxhash.h`, `zstd_deps.h`, `zstd_internal.h`,
  `zstd_trace.h`
- `lib/decompress/`: `huf_decompress.c`, `zstd_ddict.c`, `zstd_ddict.h`,
  `zstd_decompress.c`, `zstd_decompress_block.c`, `zstd_decompress_block.h`,
  `zstd_decompress_internal.h`
- Top-level `LICENSE`

`xxhash.c` is compiled as its own translation unit, as upstream's library
build does; the single-file recipe inlines it instead.

## What is dropped

- `lib/compress/`, `lib/dictBuilder/`, `lib/legacy/`, `lib/deprecated/`,
  `lib/dll/`: no compressor, dictionary builder, pre-v0.8 frame formats, or
  DLL wrappers are needed to read SPZ v4.
- `lib/common/pool.*`, `lib/common/threading.*`: the decoder is
  single-threaded.
- `lib/decompress/huf_decompress_amd64.S`: an x86-64 speed-up only, disabled
  by `ZSTD_DISABLE_ASM=1` so one C build serves MSVC, Clang, and GCC.
- Every upstream build file, program, test, and document except `LICENSE`.

No upstream source file is edited. Trimming is by omitted files and build
definitions only.

## Build definitions

Set on `gaussianSpzDecoder` in `plugins/gaussian-spz/CMakeLists.txt`
(ADR 0002 item 4):

| Definition | Why |
| --- | --- |
| `ZSTD_LEGACY_SUPPORT=0` | No pre-v0.8 frames. The legacy decoders are not vendored. |
| `ZSTD_DISABLE_ASM=1` | No assembly; one C build on every target |
| `ZSTD_TRACE=0`, `DEBUGLEVEL=0` | No tracing hooks, no debug output |
| `ZSTD_STRIP_ERROR_STRINGS` | The reader maps error codes to its own `GSPZ-` diagnostics |
| `XXH_NAMESPACE=ZSTD_` | Prefixes the bundled xxHash so it cannot clash with another xxHash |
| `ZSTDLIB_VISIBLE=`, `ZSTDERRORLIB_VISIBLE=` | Stop `zstd.h` from forcing default visibility |

The target also sets `C_VISIBILITY_PRESET hidden`, so no zstd (or miniz)
symbol leaves `libGaussianSpzFileFormat`. The CTest
`gaussianSpz_no_exported_decoder_symbols` checks the built library on every
target.

## Binary size

The size of `libGaussianSpzFileFormat` in the release products, before and
after the decoder was added (ADR 0002, *Package change*). "Before" is the
published v0.10.0 product; the plugin and library sources did not change
between v0.10.0 and the vendoring change. "After" is the release workflow's
dry run of the vendoring change (run `44111232343`, 2026-11-17, commit
`0f26227`). Sizes are in bytes.

| Target | Before (v0.10.0) | After | Change |
| --- | ---: | ---: | ---: |
| Windows x86-64 (MSVC 143) | 235,008 | 336,896 | +101,888 (+43%) |
| Ubuntu 24.04 x86-64 (GCC) | 422,448 | 651,376 | +228,928 (+54%) |
| macOS 15 arm64 (Apple Clang) | 309,216 | 416,576 | +107,360 (+35%) |

The Linux library in the same dry run exports no vendored decoder symbol;
the v0.10.0 one exported 40 miniz symbols.

## Re-vendoring

1. Download the target release archive and its `.sha256`; check the digest
   and record the tag, commit, and digest above.
2. Replace every file listed under *What is vendored* from the archive's
   `lib/` and top level. Check upstream's `zstddeclib-in.c` for added or
   removed decoder sources, and the include graph for new headers.
3. Update the version and digest in both `THIRD_PARTY_NOTICES.md` files.
4. Rebuild `gaussian-spz` and run its CTests, including the exported-symbol
   check and the v4 reader fixtures.

A zstd release that fixes an advisory affecting the decompression path is
re-vendored this way before the next release.
