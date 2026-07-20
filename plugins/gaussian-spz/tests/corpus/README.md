# Real-asset corpus

Small subsets of real Gaussian Splatting SPZ exports, committed for
tolerance-based semantic tests. Synthetic fixtures in
[`../fixtures`](../fixtures) remain the primary, exact-assertion coverage;
corpus assets supplement them and must never be the sole source of coverage
(design policy §17).

The admission rules are the same four the
[PLY corpus](../../../gaussian-ply/tests/corpus/README.md) applies — license,
provenance, deterministic derivation, size — with SPZ-specific readings of the
last two:

- **Deterministic derivation.** Assets are produced by
  [`scripts/spz_subset.py`](../../../../scripts/spz_subset.py) with recorded
  arguments. The tool selects on the quantized payload and copies retained
  per-point records byte-for-byte, so the *decompressed payload* regenerates
  exactly; `output.payload_sha256` in the provenance record is the check that
  holds across machines. The container hash `output.sha256` pins one specific
  write and may differ under another zlib build, since deflate output is not
  standardized.
- **Size.** SPZ is quantized and gzip-framed, so an 8,192-Gaussian degree-3
  subset is under 200 KB — roughly a tenth of the equivalent PLY asset.

## Layout

```
corpus/
  <asset-name>/
    <asset-name>-<derivation>.spz                 committed subset
    <asset-name>-<derivation>.spz.provenance.json machine-readable derivation record
    PROVENANCE.md                                 capture, producer, and license record
```

`test_gaussian_spz_plugin.py` discovers every `corpus/*/*.spz`, reads the
expected Gaussian count and SH degree from the provenance record, and checks
the decoded stage semantically (finite values, positive scales, normalized
orientations, opacity range, extent containment). Corpus assets are
intentionally not declared in `openstrata.plugin.yaml`, so they are not
packaged into the distributed bundle.

## Relationship to the PLY corpus

The two corpora share subject names (`yashica-t4`, `leica-sofort`) because the
same objects were captured, but each asset comes from an independent capture
through a different pipeline — Scaniverse on-device for SPZ, COLMAP plus Brush
for PLY. They are not two encodings of one reconstruction. Cross-format
equivalence is proven by the synthetic PLY/SPZ fixture pairs, which encode one
known model into both containers; these assets cannot serve that purpose and
must not be diffed against their PLY namesakes.
