# Benchmark corpus

The inputs that large-asset validation is measured against, and the resource
targets each measurement is checked against. This is the corpus the design
policy [§12.1](../design/DESIGN_POLICY.md#121-measure-before-redesigning)
asks for — a small synthetic fixture, a medium real asset, and large
references — extended to every input format and up to the shared
[import ceiling](IMPORT_LIMITS.md).

The corpus is declared machine-readably in
[`tests/benchmark/corpus.json`](../../tests/benchmark/corpus.json) and has one
tool, [`scripts/benchmark_corpus.py`](../../scripts/benchmark_corpus.py), which
lists, verifies, generates, and measures it. Each measurement goes through
[`scripts/benchmark_import.py`](../../scripts/benchmark_import.py), one asset per
process, so every row comes from the seam
[PERFORMANCE_BASELINES.md](PERFORMANCE_BASELINES.md) already uses.

## Tiers

| Tier | Measures | Assets | Origin | Hosted CI |
| --- | --- | ---: | --- | --- |
| `fixture` | process startup and the per-call floor | 3 — one per format | committed synthetic fixtures | ✅ |
| `corpus` | real value distributions at 8,192 Gaussians | 6 — two captures × three formats | committed CC0 subsets | ✅ |
| `scale` | how import scales to the 8,000,000 ceiling | 9 — three steps × three formats | generated from a committed CC0 subset | 1M step only |
| `real` | real exports at their native size | 8 | retained locally or downloaded on demand | never |

Every committed and generated asset may run in CI. Real assets never do: some
are not redistributable, and none is in the repository (design policy §17).

## The scale tier

No redistributable real asset reaches millions of Gaussians, and committing
one would put gigabytes into git history. The scale tier is therefore
generated, deterministically, from the committed CC0
[`yashica-t4-top8192.ply`](../../plugins/gaussian-ply/tests/corpus/yashica-t4/PROVENANCE.md)
subset:

- **Tiling.** The 8,192 source Gaussians are copied onto a cubic grid,
  filled x-fastest and centered on the origin. Only positions change per
  copy. The grid spacing is the source's largest extent plus 25 %, rounded up
  to 1/16 unit, so every offset is exact in float32 and in SPZ's 12-bit fixed
  point, and copies never overlap.
- **One source, three formats.** The PLY, SPZ, and SOG assets of a step are
  encoded from the same in-memory rows, so they describe the same Gaussians
  and their rows are comparable. PLY keeps the source's bytes and Brush
  property order with x/y/z translated. SPZ v2 goes through the reference
  serializer's quantization after the RDF→RUB conversion of
  [ADR 0001](../adr/0001-model-frame-is-rub.md). SOG v2 follows
  [SOG_MAPPING.md](SOG_MAPPING.md): positions are quantized over the whole
  grid, the scale, `sh0`, and SH codebooks are 256-entry quantile fits, and
  the `shN` palette holds one centroid per source Gaussian.

| Step | Copies | Gaussians | PLY | SPZ | SOG | Hosted CI |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `scale-1m` | 128 | 1,048,576 | 236.0 MiB | 39.5 MiB | 24.5 MiB | ✅ |
| `scale-4m` | 512 | 4,194,304 | 944.0 MiB | 157.7 MiB | 96.5 MiB | local |
| `scale-ceiling` | 976 | 7,995,392 | 1,799.5 MiB | 300.6 MiB | 183.5 MiB | local |

`scale-ceiling` is the largest whole number of copies under the 8,000,000
declared-count ceiling, so the top step is an input the importer must accept.
Above it, the [hostile-input fixtures](IMPORT_LIMITS.md#test-coverage) already
cover refusal.

### What the scale tier represents

Gaussian count, SH degree, attribute layout, and the per-Gaussian value
distributions are those of a real capture. Costs that follow the Gaussian count
are therefore representative: allocation, frame and value conversion, shared
validation, USD authoring, flattening, and peak memory.

Costs that follow the source *bytes* are not. Every attribute except position
repeats with an 8,192-Gaussian period:

- SPZ's deflate finds those repeats in the short alpha, color, scale, and
  rotation streams, so the container is smaller, and inflates faster, than a
  real export of the same count.
- SOG planes are written by the SOG fixture generator's VP8L encoder: an
  uncompressed flat code, four bytes per texel, in a stored ZIP. The archive
  is larger than SplatTransform output, and WebP decoding takes the literal
  path rather than the entropy-coded one. The quantile codebooks and the
  8,192-centroid palette are not k-means fits.

Source size and the compressed-container decode paths are measured on the
`corpus` and `real` tiers, never inferred from the `scale` tier.

### Determinism

`corpus.json` records each generated asset's hash, and `generate` and `run`
check it every time. A mismatch is a failure, not a refresh: it means
generation stopped reproducing the recorded bytes. PLY and SOG are checked by
file SHA-256. SPZ is checked by the SHA-256 of its decompressed payload,
because deflate output varies between zlib builds; the SPZ corpus records the
same check for the same reason. `generate --record` rewrites the hashes, and
must be used only together with a change to the generator.

All nine assets were decoded on 2026-11-03 (local Linux) by the three
bundles' decoders, outside USD, with zero
[`CheckCloudContract`](../../libs/gaussian-core/include/openstrata/gs/testing/CloudContract.h)
violations. Decoded positions agree across the three formats within each
format's quantization step.

## The real tier

| Asset | Producer | Gaussians | License | Availability |
| --- | --- | ---: | --- | --- |
| `real-yashica-t4-ply` | Brush v0.3.0 | 59,559 | CC0-1.0 | retained by the author; source of the committed subset |
| `real-leica-sofort-ply` | Brush v0.3.0 | 145,912 | CC0-1.0 | retained by the author; source of the committed subset |
| `real-yashica-t4-spz` | Scaniverse | 55,662 | CC0-1.0 | retained by the author; source of the committed subset |
| `real-leica-sofort-spz` | Scaniverse | 171,397 | CC0-1.0 | retained by the author; source of the committed subset |
| `real-cactus-142k-ply` | Jawset Postshot | 139,410 | unrecorded | personal test data; local only |
| `real-cactus-2m-ply` | Jawset Postshot | 1,935,120 | unrecorded | personal test data; local only |
| `real-garden-ply` | Graphdeco reference | 5,834,784 | Gaussian-Splatting License, non-commercial | download on demand |
| `real-bicycle-ply` | Graphdeco reference | unrecorded | Gaussian-Splatting License, non-commercial | download on demand |

The four CC0 exports carry the SHA-256 their subsets' provenance records
already pin. The Graphdeco references are members of the pretrained-models
archive (`models.zip`, `<scene>/point_cloud/iteration_30000/point_cloud.ply`);
the manifest records the URL and member path, and records each hash at its
first verified download. Their license permits non-commercial research and
evaluation only, so they are never committed, cached in CI, or redistributed.
The cactus exports have no recorded license and stay local measurements; they
are never cited as a reproducible reference.

Known gaps:

- No full-size real SOG export is declared. The `scale` tier is the only large
  SOG input until a CC0 conversion of a retained export, with SplatTransform
  provenance, is recorded.
- `real-bicycle-ply` has not been measured, so its Gaussian count is
  unrecorded; `run` reports what it measures instead of gating the count.

## Targets

`corpus.json` declares the resource targets every measured row is checked
against. They are **provisional**, and are to be tightened once hosted rows
exist for every platform.

| Target | Bound | Rationale |
| --- | --- | --- |
| Gaussian count, SH degree | equal to the manifest | correctness at scale comes before speed |
| Metadata-only read | ≤ 0.25 s at any size | §12.3 requires a header-only read. Baselines are 5-13 ms, and a full decode of the 1M step takes seconds, so the bound catches a regression to a full read without depending on runner speed. |
| `CanRead()` | ≤ 0.25 s | routing must stay header-only (design policy §7.6) |
| Peak resident memory | ≤ 0.35 GiB + 0.60 GiB per million Gaussians | PLY and SPZ measure 0.41 GiB per million ([baselines](PERFORMANCE_BASELINES.md)). 0.60 leaves room for SOG's decoded planes and for differences between operating systems' peak counters. 0.35 GiB covers the interpreter and OpenUSD. |
| `Stage.Open`, flatten | recorded, not gated | wall-clock time on shared hosted runners is not a stable gate; compare against rows from the same machine |

## Running

`verify` needs only Python. `run` needs OpenUSD's Python modules and all three
plugins on `PYTHONPATH` and `PXR_PLUGINPATH_NAME`, exactly like the Python
integration tests.

```sh
python scripts/benchmark_corpus.py list
python scripts/benchmark_corpus.py verify
python scripts/benchmark_corpus.py run --ci --report benchmark-report.json
python scripts/benchmark_corpus.py run --tier scale --work /path/to/scratch
python scripts/benchmark_corpus.py run --tier real --assets /path/to/real-assets
```

Without `--work`, each generated asset is written to a temporary directory
just before it is measured and is deleted afterwards. `--work` keeps them, and
reuses any whose hash still matches. `local` and `download` assets are looked
up by file name in `--assets` (or `USD3DGS_BENCHMARK_ASSETS`), and are
reported as `unavailable` when absent; `--require-all` makes that a failure.
Generating every step takes about two minutes and 3.7 GiB of disk.

## Hosted gate

The composed root build registers two CTest cases, which the
`workspace-ctest-pr-*` cells run on Windows, macOS, and Linux:

- `benchmark_corpus_manifest` runs `verify`: the manifest's consistency, the
  committed assets' hashes, and the planar WebP encoder the scale tier uses,
  checked byte-for-byte against the fixture generator's reference encoder.
- `benchmark_corpus_ci` runs `run --ci`: the fixture and corpus tiers and the
  1M scale step in all three formats, gated on the targets above. The JSON
  report is written to `tests/benchmark/benchmark-report.json` in the build
  tree.

The 4M and ceiling steps and the real tier are local measurements. The ceiling
PLY alone is 1.8 GiB, plus its flattened USDC, and the real tier cannot be
distributed to a runner.

## Admitting an asset

- Record a license. Only `committed` and `generated` assets may set `ci`.
- Real assets are never committed. Record the producer and availability, and
  record the SHA-256 at the first verified acquisition.
- A declared count must not exceed the import ceiling, which `verify` checks.
- A new generated step needs only a tile count. Write its hashes with
  `generate --record` and commit the manifest change.
