# Reference-written `.splat` file

A third-party `.splat` file kept as an independent check on the decoder. It
is outside `tests/fixtures/`, so it is not installed into the plugin package,
and it is not declared in `openstrata.plugin.yaml`.

| File | SHA-256 |
| --- | --- |
| `splattransform-minimal.splat` | `43410dad38d82e7e3b8992750ba891641b86f3bb10011d6f0e3419836dcfd38f` |

## Provenance

- Source: [SplatTransform](https://github.com/playcanvas/splat-transform),
  `test/fixtures/splat/minimal.splat` at tag `v3.11.2` (commit
  `6f642939dd5d`), renamed here. The file was added in commit `2e691bd40df9`
  (2026-01-14) and written by `test/fixtures/create-fixtures.mjs`.
- Four records on a 2 x 2 grid in the XZ plane: positions `(-1, 0, -1)`,
  `(0, 0, -1)`, `(-1, 0, 0)`, and `(0, 0, 0)`; linear scale 0.1 on every
  axis; colour bytes from a position gradient; alpha byte 230.
- Examined in the
  [FMT-13 spike](../../../../docs/reports/format-spike-splat-2026-11-19.md)
  §8. The generator's comment calls the rotation bytes `128, 128, 128, 255`
  an identity in `x, y, z, w` order. Read in the format's `w, x, y, z`
  order, they are a 180-degree rotation about Z. The Gaussians are
  isotropic, so the rendering is unaffected, and the test does not use the
  file to check orientation.

## Use

`test_gaussian_splat_decoder.cpp` (`TestReferenceWritten`) decodes the file
and checks positions, scales, colour, and opacity against the values above,
through a file this repository did not write.

## Licence

MIT, copyright PlayCanvas Ltd. The full licence text is in
[`LICENSE.splat-transform`](LICENSE.splat-transform), copied from the same
commit.
