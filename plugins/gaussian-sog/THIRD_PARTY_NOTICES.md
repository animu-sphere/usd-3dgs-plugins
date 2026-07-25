# Third-Party Notices

The `gaussian-sog` plugin includes two vendored components.

## libwebp 1.6.0

Vendored from tag `v1.6.0` (commit
`4fa21912338357f89e4fd51cf2368325b59e9bd9`), **decoder subset only**: the
`dec/` sources plus the decode-path `dsp/` and `utils/` sources and the public
`webp/` headers. The encoder, mux, demux, and `sharpyuv` trees are not
vendored.

libwebp is distributed under the BSD-3-Clause license, with a separate
additional patent grant. Both texts, together with the upstream `AUTHORS` file
and a record of exactly what was kept and dropped, are preserved under
`third_party/libwebp` in the source repository. The plugin uses only lossless
(VP8L) decoding, for the SOG property planes; a lossy bitstream is rejected as
malformed rather than decoded approximately.

Upstream: <https://github.com/webmproject/libwebp>

## miniz 3.0.2

Vendored from the upstream release archive `miniz-3.0.2.zip`
(SHA-256 `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`).

miniz is distributed under the MIT license. Its upstream license text, README,
and changelog are preserved under `third_party/miniz` in the source repository.
The plugin uses miniz's ZIP archive *reading* and inflate paths for bundled
`.sog` files; archive writing, stdio, and time facilities are compiled out. The
`meta.json` parsing and every Gaussian-specific dequantization are done by this
repository's own reader and decoder.

Upstream: <https://github.com/richgel999/miniz>
