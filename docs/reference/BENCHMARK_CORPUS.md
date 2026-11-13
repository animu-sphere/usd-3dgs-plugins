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
| `real` | real captures at their native size | 9 | retained exports, one local conversion, and references available on demand | never |

Every committed and generated asset may run in CI. Real assets never do: some
are not redistributable, and none is in the repository (design policy §17).

## The scale tier

None of the redistributable real assets in this corpus reaches millions of
Gaussians, and committing one at that size would put gigabytes into git
history. The scale tier is therefore
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
| `real-leica-sofort-sog` | SplatTransform v3.1.6 | 145,912 | CC0-1.0 | local conversion of the full Brush export |
| `real-cactus-142k-ply` | Jawset Postshot | 139,410 | unrecorded | personal test data; local only |
| `real-cactus-2m-ply` | Jawset Postshot | 1,935,120 | unrecorded | personal test data; local only |
| `real-garden-ply` | Graphdeco reference | 5,834,784 | Gaussian-Splatting License, non-commercial | download on demand |
| `real-bicycle-ply` | Graphdeco reference | 6,131,954 | Gaussian-Splatting License, non-commercial | download on demand |

The four CC0 exports carry the SHA-256 their subsets' provenance records
already pin. The Graphdeco references are members of the pretrained-models
archive (`models.zip`, `<scene>/point_cloud/iteration_30000/point_cloud.ply`).
The manifest records the URL, member paths, and SHA-256 hashes of both members
extracted for the local run. Their license permits non-commercial research
and evaluation only, so they are never committed, cached in CI, or
redistributed. The cactus exports have no recorded license and stay local
measurements.

The Leica Sofort SOG was made from the hash-verified `camera_02_30000.ply`
using SplatTransform v3.1.6 (`04b6d15`):

```sh
splat-transform --no-tty camera_02_30000.ply leica-sofort-full.sog
```

The manifest records the measured output's hash. This exercises SplatTransform
on a full capture; the `scale` tier is still the only multi-million SOG input.

## Targets

`corpus.json` declares the resource targets every measured row is checked
against. The limits use the [hosted](#hosted-gate) and
[local](#local-measurements) measurements as a baseline, with room for
runner and filesystem variation.

| Target | Bound | Rationale |
| --- | --- | --- |
| Gaussian count, SH degree | equal to the manifest | correctness at scale comes before speed |
| Metadata-only read | ≤ 0.15 s at any size | §12.3 rules out decoding the Gaussian payload. The slowest measured row was the local ceiling SOG at 54.7 ms, including the ZIP read. |
| `CanRead()` | ≤ 0.10 s | Routing reads a small prefix (design policy §7.6). All hosted rows finished below 1 ms, and all local rows below 7 ms. The limit leaves room for file open latency. |
| Peak resident memory | ≤ 0.60 GiB + 0.50 GiB per million Gaussians | The local 4M and ceiling rows measured about 0.41 GiB per million. The 1M PLY peak on macOS ranged from 0.783 to 0.985 GiB across two runs. The fixed allowance leaves room for that variation under a 1.124 GiB limit at 1M. |
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

First hosted run, 2026-11-03 (commit `e4cc662`): `benchmark_corpus_ci`
passed in all three cells — Linux in 9.4 s, Windows in 17.8 s, macOS in
20.4 s, including generation. Every row therefore met its count, SH degree,
metadata-only, `CanRead()`, and peak-memory gates on every platform. That run
did not retain the per-row numbers. The workspace jobs now include
`build/*/tests/benchmark/benchmark-report.json` in their `report-<cell>`
artifacts.

The first run with the reports saved was on 2026-11-13 JST (commit
`99bbd0b`, OpenUSD 26.08). Each artifact contained the expected 12 rows, and
all 36 rows passed the original limits. The two timing maxima below cover
all 12 rows per runner. The 1M columns list PLY, SPZ, and SOG in that order.
See [source CI run 75](https://github.com/animu-sphere/usd-3dgs-plugins/actions/runs/43250313447).

| Runner | Max `CanRead()` | Max metadata-only | 1M `Stage.Open` (s) | 1M peak resident (GiB) |
| --- | ---: | ---: | ---: | ---: |
| Windows 2022 x86-64 | 0.39 ms | 20.4 ms | 5.162 / 1.315 / 0.883 | 0.473 / 0.473 / 0.473 |
| macOS 15 arm64 | 0.55 ms | 12.3 ms | 4.192 / 0.679 / 0.376 | 0.783 / 0.633 / 0.585 |
| Ubuntu 24.04 x86-64 | 0.31 ms | 32.4 ms | 3.213 / 0.939 / 0.606 | 0.532 / 0.516 / 0.587 |

A [second run](https://github.com/animu-sphere/usd-3dgs-plugins/actions/runs/43352463886)
put the macOS 1M PLY peak at 0.985 GiB, above the proposed 0.927 GiB budget.
The memory formula above accounts for that result while setting lower
limits for the 4M and ceiling rows than the original formula. Those larger
rows were measured on local Windows.

The 4M and ceiling steps and the real tier run locally. The ceiling PLY alone
is 1.8 GiB, plus its flattened USDC, and the real tier is kept out of hosted CI.

## Local measurements

Measured on 2026-11-13 with Windows 11 Pro 25H2, a Ryzen 9 7950X, OpenUSD
26.08 (`cy2026/usd`), Python 3.13.14, and a CMake Release build with MSVC
19.51 (`/O2`). Each row is one run in a fresh process. Times are useful for
comparing rows on this machine; they are not cross-platform timing gates.

| Scale asset | Gaussians | `CanRead()` | Metadata-only | `Stage.Open` | Flatten to USDC | Peak resident |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `scale-1m-ply` | 1,048,576 | 5.9 ms | 7.5 ms | 2.90 s | 0.14 s | 0.49 GiB |
| `scale-1m-spz` | 1,048,576 | 5.8 ms | 11.9 ms | 0.49 s | 0.16 s | 0.49 GiB |
| `scale-1m-sog` | 1,048,576 | 0.3 ms | 16.9 ms | 0.32 s | 0.16 s | 0.49 GiB |
| `scale-4m-ply` | 4,194,304 | 4.9 ms | 7.8 ms | 12.66 s | 0.51 s | 1.74 GiB |
| `scale-4m-spz` | 4,194,304 | 5.5 ms | 13.1 ms | 1.91 s | 0.63 s | 1.74 GiB |
| `scale-4m-sog` | 4,194,304 | 0.2 ms | 31.3 ms | 1.20 s | 0.58 s | 1.74 GiB |
| `scale-ceiling-ply` | 7,995,392 | 6.2 ms | 7.4 ms | 23.63 s | 0.98 s | 3.26 GiB |
| `scale-ceiling-spz` | 7,995,392 | 4.5 ms | 12.7 ms | 3.69 s | 1.27 s | 3.26 GiB |
| `scale-ceiling-sog` | 7,995,392 | 0.2 ms | 54.7 ms | 2.29 s | 1.20 s | 3.26 GiB |

All nine rows passed. At 4M and the ceiling, peak memory was about 0.41 GiB
per million Gaussians for all three formats. Bundled SOG metadata reads grew
from 16.9 to 54.7 ms: the reader loads the ZIP into memory before extracting
`meta.json`, though it does not decode any WebP planes. The generated SOG
and SPZ `Stage.Open` times reflect the [scale encodings](#what-the-scale-tier-represents).

| Real asset | Source | `CanRead()` | Metadata-only | `Stage.Open` | Flatten to USDC | Peak resident |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `real-yashica-t4-ply` | 13.4 MiB | 0.2 ms | 7.5 ms | 0.17 s | 0.02 s | 0.10 GiB |
| `real-leica-sofort-ply` | 32.8 MiB | 0.5 ms | 8.2 ms | 0.40 s | 0.02 s | 0.13 GiB |
| `real-yashica-t4-spz` | 1.1 MiB | 0.3 ms | 12.4 ms | 0.02 s | 0.02 s | 0.09 GiB |
| `real-leica-sofort-spz` | 3.8 MiB | 0.3 ms | 12.3 ms | 0.07 s | 0.05 s | 0.14 GiB |
| `real-leica-sofort-sog` | 4.6 MiB | 0.2 ms | 9.8 ms | 0.07 s | 0.03 s | 0.13 GiB |
| `real-cactus-142k-ply` | 31.4 MiB | 0.2 ms | 7.5 ms | 0.39 s | 0.02 s | 0.13 GiB |
| `real-cactus-2m-ply` | 435.5 MiB | 0.2 ms | 7.4 ms | 5.42 s | 0.25 s | 0.84 GiB |
| `real-garden-ply` | 1,380.0 MiB | 0.3 ms | 7.3 ms | 17.28 s | 0.71 s | 2.39 GiB |
| `real-bicycle-ply` | 1,450.3 MiB | 0.2 ms | 6.4 ms | 18.01 s | 0.75 s | 2.51 GiB |

All nine rows passed. The SOG conversion reduced the Leica Sofort source
from 32.8 to 4.6 MiB and opened in 0.07 s versus 0.40 s for its source PLY.
Both peaked at about 0.13 GiB. The Leica Sofort SPZ comes from a separate
capture and is not another encoding of those Gaussians. Garden and Bicycle
both measured about 0.41 GiB per million Gaussians, in line with the scale
PLY rows.

## Admitting an asset

- Record a license. Only `committed` and `generated` assets may set `ci`.
- Real assets are never committed. Record the producer and availability, and
  record the SHA-256 at the first verified acquisition.
- A declared count must not exceed the import ceiling, which `verify` checks.
- A new generated step needs only a tile count. Write its hashes with
  `generate --record` and commit the manifest change.
