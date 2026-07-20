# Third-Party Notices

The `gaussian-spz` plugin includes miniz 3.0.2, vendored from the upstream
release archive `miniz-3.0.2.zip`
(SHA-256 `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`).

miniz is distributed under the MIT license. Its upstream license text, README,
and changelog are preserved under `third_party/miniz` in the source
repository. The plugin uses only miniz's raw-DEFLATE decompression and CRC32;
the SPZ gzip member framing is parsed by this repository's own reader.

Upstream: <https://github.com/richgel999/miniz>
