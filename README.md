# USD 3DGS Plugins

OpenUSD file-format plugins that import
[3D Gaussian Splatting](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/)
assets as standard USD stages: open a trained `.ply` or Niantic `.spz` capture
directly in `usdview` or any USD pipeline as OpenUSD 26.05's
`ParticleField3DGaussianSplat` schema.

<p align="center">
  <img src="docs/assets/usd3dgs_with_hdparticlefield.gif" alt="Imported Gaussian PLY in usdview" width="700" />
  <br>
  <p align="center"><i>A Gaussian PLY imported by this plugin and displayed in usdview by a Hydra
  renderer that supports the OpenUSD particle-field Gaussian schema.</i></p>
</p>

**What it does**

- Read-only import of Graphdeco-style Gaussian PLY (ASCII and binary
  little-endian, SH degrees 0-3) into `/Asset/Splat` with positions, scales,
  orientations, opacities, and spherical-harmonic radiance.
- Read-only import of Niantic SPZ (container versions 1-3, SH degrees 0-3),
  dequantized and converted into the same `/Asset/Splat` stage. PLY and SPZ
  author the identical hierarchy, schema, and metadata through one shared
  writer.
- Header-only metadata reads (~5 ms at any size), PLY import arguments
  (`shDegree`, `opacityThreshold`, `scaleMultiplier`), and stable
  `GSPLY-****` / `GSPZ-****` diagnostics with machine-readable catalogs.
- Import only — rendering is owned by the sibling project
  [`hydra-merlin`](https://github.com/animu-sphere/hydra-merlin).

**What it reads**

`gaussian-ply` — Graphdeco-style `.ply`:

| Exporter | Status |
| --- | --- |
| Graphdeco reference (INRIA) | ✅ verified (Mip-NeRF 360 `garden`, 5.8M Gaussians) |
| Brush | ✅ verified (committed CC0 corpus) |
| Jawset Postshot | ✅ verified (up to 1.9M Gaussians) |
| SuperSplat uncompressed export | ☑ layout-compatible, fixture-covered |
| SuperSplat *compressed* `.ply`, `.splat`, `.ksplat` | ❌ rejected explicitly ([details](docs/reference/PLY_DIALECTS.md)) |

`gaussian-spz` — Niantic `.spz`:

| Container | Status |
| --- | --- |
| SPZ versions 1-3 (gzip) | ✅ decoded and fixture-covered; real-asset corpus in progress |
| SPZ version 4 (ZSTD) | ❌ rejected with a specific unsupported-version diagnostic |

Full tables: [dialect compatibility](docs/reference/PLY_DIALECTS.md) ·
[capability matrix](docs/reference/CAPABILITY_MATRIX.md) ·
[performance baselines](docs/reference/PERFORMANCE_BASELINES.md).

## Quick start

Requirements: OpenUSD `>=26.05,<27.0` and a C++17 compiler. Build and test
with plain CMake against any OpenUSD 26.05 installation:

```sh
cmake --preset default -DCMAKE_PREFIX_PATH=/path/to/openusd
cmake --build --preset default
ctest --test-dir build/default --output-on-failure
```

The repo is dual-mode: with
[OpenStrata](https://github.com/animu-sphere/open-strata) `ost` 0.18.0 the
same tree gets the full verification ladder, packaging, and viewer tooling —
see [BUILDING.md](docs/guides/BUILDING.md):

```sh
ost runtime pull cy2026 --profile usd
ost plugin build plugins/gaussian-ply
ost plugin test plugins/gaussian-ply --up-to 5
```

Open a capture in usdview:

```powershell
ost plugin view plugins\gaussian-ply "C:\path\to\scene.ply"
```

Or convert it to a compact binary USD file:

```powershell
ost plugin run plugins\gaussian-ply -- usdcat --flatten --skipSourceFileComment --usdFormat usdc --out scene.usd scene.ply
```

Installing a packaged release into an existing OpenUSD environment is covered
in [INSTALL.md](docs/guides/INSTALL.md).

## Status

v0.2.0 — production-ready Graphdeco PLY import — is tagged and published;
the current target is **v0.3.0 — SPZ import**. SPZ decoding and USD authoring
are implemented and pass through the same `GaussianCloudData` pipeline as PLY;
the remaining v0.3.0 work is a redistributable SPZ corpus asset, PLY/SPZ
equivalence fixtures, performance baselines, and release hardening
([current plan](docs/roadmap/current.md)). Releases are tag-driven,
digest-reproducible, and published as drafts for human review
([release records](docs/releases/README.md)).

## Architecture

```text
.ply -> PlyReader (tinyPLY adapter)     -> GaussianPlyDecoder --\
.spz -> SpzReader (miniz DEFLATE/gzip)  -> GaussianSpzDecoder --/
                                                                |
                        GaussianCloudData (gaussianCore; no USD types)
                        -> GaussianLayerWriter (libs/gaussian-usd)
                        -> UsdVolParticleField3DGaussianSplat
```

The workspace separates format-independent Gaussian modelling
(`libs/gaussian-core`) and shared USD authoring (`libs/gaussian-usd`) from
format-specific import (`plugins/gaussian-ply`, `plugins/gaussian-spz`), each
independently buildable and testable. It is
built, tested, packaged, and released with OpenStrata's `ost` CLI
(measured usage: [dogfooding reports](docs/reports/ost/)) and remains
dual-mode with plain CMake. The sibling project
[`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins) shares
this layout and process. Policy: [DESIGN_POLICY.md](docs/design/DESIGN_POLICY.md).

## Contributing and documentation

The minimal development path is in [CONTRIBUTING.md](CONTRIBUTING.md); the
documentation index is [docs/README.md](docs/README.md). The normative mapping
contracts are [PLY_MAPPING.md](docs/reference/PLY_MAPPING.md) and
[SPZ_MAPPING.md](docs/reference/SPZ_MAPPING.md).

## License

Project code is Apache-2.0. SPZ is implemented from Niantic's published, MIT
-licensed specification rather than vendored. Third-party retained notices and
fixed source revisions (tinyPLY, miniz) are documented in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
