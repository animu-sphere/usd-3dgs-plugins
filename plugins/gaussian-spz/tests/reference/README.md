# Reference-written SPZ files

Third-party SPZ files kept as an independent check on the reader. They are
outside `tests/fixtures/`, so they are not installed into the plugin package,
and they are not declared in `openstrata.plugin.yaml`.

| File | SHA-256 |
| --- | --- |
| `splattransform-minimal-v3.spz` | `3b2a493c12a625b05ad80d1767c4f59d25087750d8574989873be4ad85493704` |
| `splattransform-minimal-v4.spz` | `4629adb9f96c4fc6cdc26be5c0fd76bdbf473f47694834dd9e601d34a23ca463` |

## Provenance

- Source: [SplatTransform](https://github.com/playcanvas/splat-transform),
  `test/fixtures/splat/minimal-v3.spz` and `minimal-v4.spz` at tag `v3.11.2`
  (commit `6f642939dd5d`), renamed here.
- Written by `test/fixtures/create-fixtures.mjs` in commit `7112e8321abf`
  (2026-05-08) through `@adobe/spz` 0.2.1, a WebAssembly build of the
  reference library `nianticlabs/spz`. Both files encode the same 4-point,
  SH degree 0 cloud: one as a gzip v3 container, one as a v4 zstd container.
- Examined in the
  [FMT-05 spike](../../../../docs/reports/format-spike-spz-v4-2026-11-17.md)
  §3: each v4 stream is byte-identical to the matching v3 attribute section.

## Use

`test_gaussian_spz_decoder.cpp` (`TestReferenceWrittenVersion4`) decodes both
files and requires identical clouds. The v3 path predates v4 support, so the
test ties the new zstd path to a decoder that was already verified, through
a file this repository did not write.

## Licence

MIT, copyright PlayCanvas Ltd. The full licence text is in
[`LICENSE.splat-transform`](LICENSE.splat-transform), copied from the same
commit.
