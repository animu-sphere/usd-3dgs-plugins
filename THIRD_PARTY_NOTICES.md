# Third-party notices

## tinyPLY 2.3.4

- Project: <https://github.com/ddiakopoulos/tinyply>
- Vendored commit: `40aa4a0ae9e9c203e11893f78b8bcaf8a50e65f0`
- Files: `third_party/tinyply/tinyply.h`, `tinyply.cpp`, and upstream README
- License: public-domain dedication with a permissive fallback grant

Upstream notice:

> This software is in the public domain. Where that dedication is not
> recognized, you are granted a perpetual, irrevocable license to copy,
> distribute, and modify this file as you see fit.

Authored by Dimitri Diakopoulos. The complete upstream notice is retained at
the top of `third_party/tinyply/tinyply.h` and in the vendored README.

## miniz 3.0.2

- Project: <https://github.com/richgel999/miniz>
- Vendored from: release archive `miniz-3.0.2.zip`
  (SHA-256 `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`)
- Files: `third_party/miniz/miniz.h`, `miniz.c`, upstream `LICENSE`,
  `readme.md`, and `ChangeLog.md`
- License: MIT

miniz provides the raw-DEFLATE decompression and CRC32 used by the
`gaussian-spz` container reader; the gzip member framing itself is parsed in
this repository. The complete MIT license text is retained at
`third_party/miniz/LICENSE`.
