# Performance baselines

The design-policy [§12.1](../design/DESIGN_POLICY.md#121-measure-before-redesigning)
import measurements. These are the reference numbers optimization decisions
are made against; re-measure with
[`tools/benchmark_import.py`](../../plugins/gaussian-ply/tools/benchmark_import.py)
(one asset per process, so peak resident memory is attributable) before
claiming a regression or an improvement.

## Environment

Measured 2026-07-19 at v0.2.0 development head:

- AMD Ryzen 9 7950X (16 cores), 128 GiB RAM, NVMe SSD, Windows 11 Pro
- OpenUSD 26.05 (cy2026 `usd` profile runtime), Python 3.13, MSVC 14.34
- Release build of the gaussian-ply plugin, warm file cache

## Results

| Asset | Exporter | Gaussians | SH | Source | `CanRead()` | Metadata-only read | `Usd.Stage.Open` | Flatten → USDC | USDC size | Peak resident |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `three-gaussian-binary-le.ply` (fixture) | synthetic | 3 | 1 | <0.1 MiB | 0.2 ms | 5.6 ms | <0.01 s | <0.01 s | <0.1 MiB | 0.07 GiB |
| `yashica-t4-top8192.ply` (corpus) | Brush v0.3.0 | 8,192 | 3 | 1.8 MiB | 0.2 ms | 5.6 ms | 0.03 s | 0.01 s | 1.8 MiB | 0.07 GiB |
| `camera_01_30000.ply` (local) | Brush v0.3.0 | 59,559 | 3 | 13.4 MiB | 0.3 ms | 5.4 ms | 0.16 s | 0.02 s | 13.4 MiB | 0.09 GiB |
| `cactus_..._142k_splats.ply` (local) | Postshot | 139,410 | 3 | 31.4 MiB | 0.2 ms | 5.3 ms | 0.38 s | 0.03 s | 31.4 MiB | 0.12 GiB |
| `cactus_..._2M_splats.ply` (local) | Postshot | 1,935,120 | 3 | 435.5 MiB | 0.2 ms | 5.2 ms | 5.25 s | 0.27 s | 435.5 MiB | 0.84 GiB |
| Mip-NeRF 360 `garden` `point_cloud.ply` (local) | Graphdeco reference | 5,834,784 | 3 | 1,380 MiB | 0.2 ms | 5.4 ms | 16.4 s | 0.81 s | 1,313 MiB | 2.39 GiB |

Reading the table:

- **Import scales linearly.** `Stage.Open` runs at roughly 2.8-2.9 s per
  million degree-3 Gaussians on this machine, and peak resident memory at
  roughly 0.4 GiB per million — consistent with the §12 loading model of one
  full-data representation plus the authored layer.
- **Metadata-only reads are size-independent.** ~5 ms whether the source is
  3 Gaussians or 5.8 million, because only the header is parsed
  (design policy §12.3).
- **`CanRead()` is header-only** and stays sub-millisecond at every size
  (first-touch on a cold cache adds tens of milliseconds of I/O).
- **USDC ≈ source size.** Both store the same float payload; USDC adds
  structure and drops the PLY normals garden carries.
- **usdview inspectability** is `Stage.Open` plus renderer synchronization.
  With the OpenUSD `hdParticleField` example renderer the corpus and cactus
  assets are interactive immediately; at garden's 5.8 M splats that CPU
  reference renderer, not the import path, dominates time-to-image.

## Corpus policy

The committed corpus stays small (two 8,192-Gaussian CC0 assets with full
provenance). The local rows are provenance-noted personal test data
(`Desktop/testdata_3dgs`) and the Graphdeco reference `garden` capture;
large references stay download-on-demand and never enter the repository
(design policy §17). Re-derive the corpus rows with `scripts/ply_subset.py`
as recorded in each asset's `PROVENANCE.md`.
