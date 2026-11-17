# Third-party notices

## tinyPLY 3.0

- Project: <https://github.com/ddiakopoulos/tinyply>
- Vendored commit: `c9bb690dfe5e9105961e9e28120c48c9ae084bc6`
- Files: `third_party/tinyply/tinyply.h`, `tinyply.cpp`, and upstream README
- License: public-domain dedication with a permissive fallback grant

Upstream notice:

> This software is in the public domain. Where that dedication is not
> recognized, you are granted a perpetual, irrevocable license to copy,
> distribute, and modify this file as you see fit.

Authored by Dimitri Diakopoulos. The complete upstream notice is retained at
the top of `third_party/tinyply/tinyply.h` and in the vendored README.

Local fixes in `tinyply.h` preserve the element and property names as separate
lookup keys, reject invalid element counts and truncated binary skips, and
clear the ASCII list count before reading it at a new width.

## miniz 3.0.2

- Project: <https://github.com/richgel999/miniz>
- Vendored from: release archive `miniz-3.0.2.zip`
  (SHA-256 `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`)
- Files: `third_party/miniz/miniz.h`, `miniz.c`, upstream `LICENSE`,
  `readme.md`, and `ChangeLog.md`
- License: MIT

miniz provides the raw-DEFLATE decompression and CRC32 used by the
`gaussian-spz` container reader — the gzip member framing itself is parsed in
this repository — and the ZIP archive *reading* used by the `gaussian-sog`
container reader for bundled `.sog` files. The two bundles compile it with
different definitions (`gaussian-spz` excludes the archive APIs entirely, and
neither enables archive writing, stdio, or time), so each carries its own
translation unit. The complete MIT license text is retained at
`third_party/miniz/LICENSE`.

## libwebp 1.6.0

- Project: <https://github.com/webmproject/libwebp>
- Vendored tag: `v1.6.0` (commit
  `4fa21912338357f89e4fd51cf2368325b59e9bd9`)
- Files: the **decoder subset only** under `third_party/libwebp/src/`
  (`dec/`, the common and decode-path `dsp/` and `utils/` sources, and the
  four `webp/` headers the decoder needs), plus upstream `COPYING`, `AUTHORS`,
  and `PATENTS`. The encoder, mux, demux, and `sharpyuv` trees are not
  vendored; `third_party/libwebp/VENDORING.md` records exactly what was kept
  and dropped.
- License: BSD-3-Clause (`third_party/libwebp/COPYING`), with the separate
  `third_party/libwebp/PATENTS` grant.

libwebp provides the lossless-WebP (VP8L) image decoding the `gaussian-sog`
container reader needs for the SOG property planes; the ZIP container, the
`meta.json` parsing, and every Gaussian-specific dequantization are done in
this repository. The subset is trimmed by excluded directories and build
configuration only — no upstream source file is edited — so an upstream
security update re-vendors cleanly. A lossy VP8 bitstream is rejected as
malformed by the reader rather than decoded approximately
([SOG_FORMAT.md](docs/reference/SOG_FORMAT.md) §3).

## Zstandard 1.5.7

- Project: <https://github.com/facebook/zstd>
- Vendored from: release archive `zstd-1.5.7.tar.gz` from the `v1.5.7`
  GitHub release (commit `f8745da6ff1a`), SHA-256
  `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`
- Files: the **decompression subset only** under `third_party/zstd/lib/`
  (`zstd.h`, `zstd_errors.h`, the decoder sources of `common/` and
  `decompress/`), plus upstream `LICENSE`. The compressor, dictionary
  builder, legacy decoders, threading pool, and assembly are not vendored;
  `third_party/zstd/VENDORING.md` records exactly what was kept and dropped.
- License: BSD-3-Clause (`third_party/zstd/LICENSE`). Zstandard is
  dual-licensed under BSD-3-Clause or GPLv2 at the licensee's choice; this
  project takes the BSD-3-Clause option and does not distribute it under the
  GPLv2.

Zstandard provides the frame decompression the `gaussian-spz` container
reader needs for SPZ version 4; the header, the table of contents, the frame
layout, and every Gaussian-specific dequantization are handled in this
repository ([ADR 0002](docs/adr/0002-vendored-zstd-decoder.md)). No upstream
source file is edited, and no zstd symbol is exported from the plugin
library.
