# USD 3DGS Plugins

OpenUSD file-format plugins that import
[3D Gaussian Splatting](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/)
assets as standard USD stages: open a trained `.ply` capture directly in
`usdview` or any USD pipeline as OpenUSD 26.05's
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
- Header-only metadata reads (~5 ms at any size), import arguments
  (`shDegree`, `opacityThreshold`, `scaleMultiplier`), and stable
  `GSPLY-****` diagnostics with a machine-readable catalog.
- Import only — rendering is owned by the sibling project
  [`hydra-merlin`](https://github.com/animu-sphere/hydra-merlin).

**What it reads**

| Exporter | Status |
| --- | --- |
| Graphdeco reference (INRIA) | ✅ verified (Mip-NeRF 360 `garden`, 5.8M Gaussians) |
| Brush | ✅ verified (committed CC0 corpus) |
| Jawset Postshot | ✅ verified (up to 1.9M Gaussians) |
| SuperSplat uncompressed export | ☑ layout-compatible, fixture-covered |
| SuperSplat *compressed* `.ply`, `.splat`, `.ksplat`, SPZ | ❌ rejected explicitly ([details](docs/reference/PLY_DIALECTS.md)) |

Full tables: [dialect compatibility](docs/reference/PLY_DIALECTS.md) ·
[capability matrix](docs/reference/CAPABILITY_MATRIX.md) ·
[performance baselines](docs/reference/PERFORMANCE_BASELINES.md).

## Quick start

Requirements: [OpenStrata](https://github.com/animu-sphere/open-strata)
`ost` 0.18.0, OpenUSD `>=26.05,<27.0`, a C++17 compiler. (No `ost`? The repo
also builds with plain CMake — see [BUILDING.md](docs/guides/BUILDING.md).)

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

v0.1.0 is tagged; the current target is **v0.2.0 — production-ready Graphdeco
PLY import** ([release plan](docs/roadmap/release-plan.md)). SPZ and further
formats are sequenced after PLY stabilizes. Releases are tag-driven,
digest-reproducible, and published as drafts for human review
([release records](docs/releases/README.md)).

## Architecture

```text
SdfFileFormat
    -> PlyReader (tinyPLY adapter)
    -> GaussianPlyDecoder
    -> GaussianCloudData (gaussianCore; no USD types)
    -> GaussianLayerWriter
    -> UsdVolParticleField3DGaussianSplat
```

The workspace separates format-independent Gaussian modelling
(`libs/gaussian-core`) from format-specific import
(`plugins/gaussian-ply`), each independently buildable and testable. It is
built, tested, packaged, and released with OpenStrata's `ost` CLI
(measured usage: [dogfooding reports](docs/reports/ost/)) and remains
dual-mode with plain CMake. The sibling project
[`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins) shares
this layout and process. Policy: [DESIGN_POLICY.md](docs/design/DESIGN_POLICY.md).

## Contributing and documentation

The minimal development path is in [CONTRIBUTING.md](CONTRIBUTING.md); the
documentation index is [docs/README.md](docs/README.md). The normative PLY
mapping contract is [PLY_MAPPING.md](docs/reference/PLY_MAPPING.md).

## License

Project code is Apache-2.0. tinyPLY's retained notice and fixed source revision
are documented in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
