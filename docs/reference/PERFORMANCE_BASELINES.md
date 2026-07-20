# Performance baselines

The design-policy [§12.1](../design/DESIGN_POLICY.md#121-measure-before-redesigning)
import measurements. These are the reference numbers optimization decisions
are made against; re-measure with
[`scripts/benchmark_import.py`](../../scripts/benchmark_import.py)
(one asset per process, so peak resident memory is attributable) before
claiming a regression or an improvement.

The harness resolves the file format from the asset's extension, so PLY and
SPZ rows come from one implementation measuring one seam. Any difference
between the tables below is a property of the formats, not of two benchmarks
that drifted apart.

## Environment

Measured 2026-07-19 at v0.2.0 development head:

- AMD Ryzen 9 7950X (16 cores), 128 GiB RAM, NVMe SSD, Windows 11 Pro
- OpenUSD 26.05 (cy2026 `usd` profile runtime), Python 3.13, MSVC 14.34
- Release build of the gaussian-ply plugin, warm file cache

The SPZ table was measured 2026-07-20 at v0.3.0 development head on the same
machine and runtime, with both plugins rebuilt from one optimized configure.
The PLY rows were re-measured in that session and reproduced their recorded
values (`camera_01_30000.ply` at 0.17 s against a recorded 0.16 s), so the two
tables are comparable.

> **Optimization flags are part of the measurement.** An earlier pass of the
> SPZ numbers was taken against a build directory configured with
> `CMAKE_BUILD_TYPE=Release` but an explicitly emptied
> `CMAKE_CXX_FLAGS_RELEASE`, so `/O2 /Ob2 /DNDEBUG` was absent. That build made
> PLY `Stage.Open` 2.4x slower than its recorded baseline and would have been
> misread as a regression. Confirm the configure emits `/O2` before trusting
> any row here; a fresh `cmake -DCMAKE_BUILD_TYPE=Release` does.

## Results — PLY

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
- **usdview inspectability** is `Stage.Open` plus renderer synchronization,
  and it requires a Hydra renderer that understands
  `ParticleField3DGaussianSplat` — currently the OpenUSD `hdParticleField`
  example delegate. Stock `usdview` opens the stage and shows the hierarchy
  and attributes, but draws no splats. With `hdParticleField` the corpus and
  cactus assets are interactive immediately; at garden's 5.8 M splats that CPU
  reference renderer, not the import path, dominates time-to-image.

## Results — SPZ

Measured 2026-07-20 at v0.3.0 development head. The two `capture` rows are the
full source exports the committed corpus assets are derived from.

| Asset | Producer | Gaussians | SH | Source | `CanRead()` | Metadata-only read | `Usd.Stage.Open` | Flatten → USDC | USDC size | Peak resident |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `decode-degree3-v2.spz` (fixture) | synthetic | 2 | 3 | <0.1 MiB | 0.1 ms | 13.3 ms | <0.01 s | <0.01 s | <0.1 MiB | 0.07 GiB |
| `yashica-t4-top8192.spz` (corpus) | Scaniverse | 8,192 | 3 | 0.2 MiB | 0.2 ms | 11.3 ms | 0.01 s | 0.01 s | 1.8 MiB | 0.07 GiB |
| `leica-sofort-top8192.spz` (corpus) | Scaniverse | 8,192 | 3 | 0.2 MiB | 0.2 ms | 9.9 ms | 0.01 s | 0.01 s | 1.8 MiB | 0.07 GiB |
| `yashica_t4.spz` (local) | Scaniverse | 55,662 | 3 | 1.1 MiB | 0.3 ms | 11.7 ms | 0.03 s | 0.02 s | 12.4 MiB | 0.09 GiB |
| `leica_sofort.spz` (local) | Scaniverse | 171,397 | 3 | 3.8 MiB | 0.2 ms | 9.8 ms | 0.11 s | 0.05 s | 38.1 MiB | 0.14 GiB |

Reading the table against PLY:

- **SPZ imports roughly 4x faster per Gaussian.** `Stage.Open` runs at about
  0.6 s per million degree-3 Gaussians against PLY's 2.8-2.9 s. Decompression
  is not the bottleneck the format's name suggests: SPZ moves 64 quantized
  bytes per Gaussian where PLY moves 236 float32 bytes, and the smaller volume
  more than repays the inflate. This is a property of the formats, not of the
  two decoders — both were measured through the same harness at the same seam.
- **Peak resident memory is identical at ~0.41 GiB per million.** Both formats
  converge on the same dequantized in-memory representation, so SPZ's on-disk
  advantage does not survive import. Sizing a machine for an import is a
  function of Gaussian count, never of source bytes.
- **Metadata-only reads cost about 2x PLY (~10-11 ms against ~5.6 ms) and stay
  size-independent.** The constant overhead is inflate setup: the SPZ header
  lives *inside* the gzip member, so reaching it requires starting a
  decompression that PLY's plaintext header does not. Flat across a 85,000x
  range of Gaussian counts confirms the bounded-inflate path holds and the
  §12.3 contract is met.
- **USDC is ~9x the SPZ source, against ~1x for PLY.** Both author the same
  float payload; only the source encodings differ. Do not size downstream
  storage from an SPZ file's size.
- **`CanRead()` stays sub-millisecond** — it inflates only enough to reach the
  magic. Cold-cache first touch adds several milliseconds of I/O at any size,
  which is I/O, not parsing.

## Corpus policy

The committed corpus stays small (two 8,192-Gaussian CC0 assets with full
provenance). The local rows are provenance-noted personal test data
(`Desktop/testdata_3dgs`) and the Graphdeco reference `garden` capture;
large references stay download-on-demand and never enter the repository
(design policy §17). Re-derive the corpus rows with `scripts/ply_subset.py`
as recorded in each asset's `PROVENANCE.md`.
