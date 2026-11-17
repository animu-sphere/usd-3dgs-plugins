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

## License texts

Every plugin package includes this file. The full license text of each
vendored component follows; tinyPLY's notice is quoted in full above.

### miniz (MIT, `third_party/miniz/LICENSE`)

```text
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### libwebp (BSD-3-Clause, `third_party/libwebp/COPYING`)

```text
Copyright (c) 2010, Google Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.

  * Neither the name of Google nor the names of its contributors may
    be used to endorse or promote products derived from this software
    without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### libwebp additional patent grant (`third_party/libwebp/PATENTS`)

```text
Additional IP Rights Grant (Patents)
------------------------------------

"These implementations" means the copyrightable works that implement the WebM
codecs distributed by Google as part of the WebM Project.

Google hereby grants to you a perpetual, worldwide, non-exclusive, no-charge,
royalty-free, irrevocable (except as stated in this section) patent license to
make, have made, use, offer to sell, sell, import, transfer, and otherwise
run, modify and propagate the contents of these implementations of WebM, where
such license applies only to those patent claims, both currently owned by
Google and acquired in the future, licensable by Google that are necessarily
infringed by these implementations of WebM. This grant does not include claims
that would be infringed only as a consequence of further modification of these
implementations. If you or your agent or exclusive licensee institute or order
or agree to the institution of patent litigation or any other patent
enforcement activity against any entity (including a cross-claim or
counterclaim in a lawsuit) alleging that any of these implementations of WebM
or any code incorporated within any of these implementations of WebM
constitute direct or contributory patent infringement, or inducement of
patent infringement, then any patent rights granted to you under this License
for these implementations of WebM shall terminate as of the date such
litigation is filed.
```

### Zstandard (BSD-3-Clause option, `third_party/zstd/LICENSE`)

```text
BSD License

For Zstandard software

Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name Facebook, nor Meta, nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
