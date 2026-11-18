# Real-asset corpus

`.splat` conversions of the committed CC0 PLY subsets, for tolerance-based
semantic tests. Synthetic fixtures in [`../fixtures`](../fixtures) remain the
primary, exact-assertion coverage; corpus assets supplement them and must
never be the sole source of coverage (design policy §17).

No real `.splat` file with a redistribution licence was found (see the
[FMT-13 spike](../../../../docs/reports/format-spike-splat-2026-11-19.md) §8),
so each asset here is derived from the committed PLY subset of the same name:

| Asset | Source |
| --- | --- |
| `yashica-t4/yashica-t4-top8192.splat` | [`yashica-t4-top8192.ply`](../../../gaussian-ply/tests/corpus/yashica-t4/PROVENANCE.md) |
| `leica-sofort/leica-sofort-top8192.splat` | [`leica-sofort-top8192.ply`](../../../gaussian-ply/tests/corpus/leica-sofort/PROVENANCE.md) |

Both sources are CC0-1.0, so the conversions are CC0-1.0 too.

## Derivation

`python plugins/gaussian-splat/tools/generate_fixtures.py --corpus` writes
both files with the reference writer's arithmetic (the antimatter15
`convert.py`: clamp, then truncate) but keeps the source order instead of
sorting by importance. The conversion is pure Python and deterministic, so
`output.sha256` in each provenance record must reproduce exactly.

## Use

- `test_gaussian_splat_plugin.py` opens every `corpus/*/*.splat` through USD
  and checks the stage against its provenance record and the semantic
  ranges.
- The cross-format equivalence suite
  (`tests/equivalence/test_splat_equivalence.cpp`) decodes each asset and its
  source PLY and compares them index by index, with the quantization
  tolerances in [EQUIVALENCE.md](../../../../docs/reference/EQUIVALENCE.md)
  and the DC clipping of SPLAT_MAPPING.md §3. Unlike the SPZ corpus, these
  files *are* two encodings of one reconstruction.

Corpus assets are not declared in `openstrata.plugin.yaml`, so they are not
packaged.
