# Vendored libwebp (decoder subset)

Provenance and the exact keep/drop record for the vendored copy. The consuming
build wiring lives in `plugins/gaussian-sog`; the dependency rationale and the
lossless-only policy are in
[docs/reference/SOG_FORMAT.md](../../docs/reference/SOG_FORMAT.md) §3, and the
licence is recorded in
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

## Source

- Upstream: <https://github.com/webmproject/libwebp>
- Tag: `v1.6.0`
- Commit: `4fa21912338357f89e4fd51cf2368325b59e9bd9`
- Archive: `libwebp-1.6.0.tar.gz` from the GitHub tag
- Licence: BSD-3-Clause (`COPYING`), plus the additional `PATENTS` grant

## What is vendored

Only the **decoder** library sources, matching upstream's own
`libwebpdecoder` composition (its `dec/`, `dsp/` and `utils/` autotools
`Makefile.am` common + decode source lists), so the subset can be re-vendored
mechanically when libwebp publishes a security update:

- `src/dec/` — the whole decoder (VP8 + VP8L container driver).
- `src/dsp/` — the common DSP sources plus every decode-path SIMD variant
  (SSE2/SSE4.1/AVX2/NEON/MSA/MIPS). Each SIMD translation unit self-guards on
  its `WEBP_USE_*` macro, so on any given target the unused variants compile to
  empty objects; no per-file architecture flag is required for correctness.
- `src/utils/` — the common (decode) utilities.
- `src/webp/` — only the four headers the decoder needs: `decode.h`,
  `types.h`, `format_constants.h`, and `mux_types.h` (included by
  `webp_dec.c` solely for the `ALPHA_FLAG` constant).
- Top-level `COPYING`, `AUTHORS`, `PATENTS`.

## What is dropped

- The encoder (`src/enc/`), and every encoder-only `dsp/` / `utils/`
  translation unit (`*enc*.c`, `cost*.c`, `ssim*.c`, `bit_writer_utils.*`,
  `huffman_encode_utils.*`, `quant_levels_utils.*`).
- `src/demux/`, `src/mux/`, and `sharpyuv/` — not reachable from a single
  still-image lossless decode.
- The autotools `Makefile.am` files (the build is CMake, from
  `plugins/gaussian-sog`).

No upstream source file is edited. Trimming is by omitted directories and build
configuration only, exactly as `third_party/miniz` is trimmed by compile
definitions.

## Lossless-only

SOG property planes are lossless WebP (VP8L) by specification; a lossy plane
would silently corrupt positions. The upstream decode driver is kept intact
(removing the VP8 path would fork the library), and the `gaussian-sog` reader
rejects a lossy VP8 bitstream as malformed rather than decoding it
approximately.

## Re-vendoring

1. Download the target tag's source archive; record the tag and commit above.
2. Replace `src/dec`, `src/dsp`, `src/utils` and the `src/webp` headers, then
   re-apply the drops listed above.
3. Refresh `COPYING`/`AUTHORS`/`PATENTS` and the version in
   `THIRD_PARTY_NOTICES.md`.
4. Rebuild `gaussian-sog` and run its decode smoke test.
