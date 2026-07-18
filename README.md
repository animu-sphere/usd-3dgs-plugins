# USD 3DGS Plugins

OpenUSD plugins for [3D Gaussian Splatting](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/)
assets.

<p align="center">
  <img src="docs/assets/usd3dgs_with_hdparticlefield.gif" alt="Imported Gaussian PLY in usdview" width="700" />
  <br>
  <p align="center"><i>A Gaussian PLY imported by this plugin and displayed in usdview by a Hydra
  renderer that supports the OpenUSD particle-field Gaussian schema.</i></p>
</p>

This repository is an OpenUSD plugin **workspace**: it separates
format-independent Gaussian modelling from format-specific import into
independently buildable, independently testable components. It currently ships
one plugin bundle and one shared library. The importer reads the common
Graphdeco Gaussian PLY dialect and authors OpenUSD 26.05's standard
`ParticleField3DGaussianSplat` schema.

Import is **read-only and fully materialized** — the plugin authors Gaussian USD
data and does not render it. Rendering is a separate concern owned by the
sibling project [`hydra-merlin`](https://github.com/animu-sphere/hydra-merlin),
a Hydra integration for the Gaussian particle-field schema.

> **Built with [OpenStrata](https://github.com/animu-sphere/open-strata).**
> The `ost` CLI is how this workspace is built, tested, packaged, and released,
> and measured usage is published in [docs/reports/ost/](docs/reports/ost/). The
> repo is **dual-mode**: everything also builds with plain CMake against any
> OpenUSD install, with no `ost` involved.
>
> Its sibling project
> [`usd-vrm-plugins`](https://github.com/animu-sphere/usd-vrm-plugins) — OpenUSD
> plugins for VRM avatars — shares this workspace layout, documentation
> taxonomy, and release process.

The repository follows the policy in
[`docs/design/DESIGN_POLICY.md`](docs/design/DESIGN_POLICY.md).

## Current status

The initial PLY vertical slice is implemented:

- OpenStrata `usd-plugin-workspace` scaffold, generated with OST 0.18.0
- `gaussian-ply` read-only `SdfFileFormat` bundle
- ASCII and binary little-endian PLY
- Gaussian dialect detection and required-property validation
- position, exponential scale, normalized quaternion, sigmoid opacity
- degree inference and RGB reconstruction for channel-major `f_rest_*`
- `/Asset/Splat` authored as `ParticleField3DGaussianSplat`
- deterministic unit, integration, negative, binary, and golden fixtures
- generated OpenStrata PR CI passing on hosted Windows, macOS arm64, and Linux
- vendored tinyPLY 2.3.4 at a fixed commit

SPZ, glTF/GLB Gaussian extensions, SOG, writing, streaming, and rendering are
outside the initial implementation; the
[release plan](docs/roadmap/release-plan.md) sequences them.

## Requirements

- OST 0.18.0 (verified baseline)
- a real `cy2026` / `usd` OpenStrata runtime
- OpenUSD `>=26.05,<27.0` (the Gaussian particle-field schema is required)
- a C++17-capable compiler

The verified local runtime is OpenUSD 26.05 on Windows x86-64 / MSVC 143 /
Python 3.13.

## Build and test

```sh
ost runtime pull cy2026 --profile usd
ost plugin build plugins/gaussian-ply
ost plugin doctor plugins/gaussian-ply
ost plugin test plugins/gaussian-ply --up-to 5
ost plugin test --workspace --up-to 5
ost plugin package plugins/gaussian-ply
```

`openstrata.ci.yaml` is the cross-platform CI contract. Regenerate the checked-in
GitHub Actions workflow after matrix changes with `ost ci generate github
--force`.

Open any compatible PLY in `usdview` with the plugin session composed by OST:

```powershell
ost plugin view plugins\gaussian-ply "C:\path\to\scene.ply"
```

The stage can be inspected even when the active Hydra renderer does not draw
Gaussian splats.

Flatten a PLY-backed stage to a compact binary USD file next to the source:

```powershell
ost plugin run plugins\gaussian-ply -- usdcat --flatten --skipSourceFileComment --usdFormat usdc --out "C:\path\to\scene.usd" "C:\path\to\scene.ply"
```

The repository also carries CTest coverage beyond OST's verification pyramid:

```sh
ctest --test-dir libs/gaussian-core/build/cy2026-windows-x86_64-py313-usd --output-on-failure
ctest --test-dir plugins/gaussian-ply/build/cy2026-windows-x86_64-py313-usd --output-on-failure
```

For a plain CMake workspace build, point `CMAKE_PREFIX_PATH` at an OpenUSD 26.05
installation:

```sh
cmake --preset default -DCMAKE_PREFIX_PATH=/path/to/openusd
cmake --build --preset default
ctest --test-dir build/default --output-on-failure
```

## Release artifacts

Pushing a tag `vX.Y.Z` (matching [`VERSION`](VERSION), every bundle manifest,
and that version's finalized `CHANGELOG.md` section) runs
[`.github/workflows/release.yml`](.github/workflows/release.yml): it builds and
verifies on the same three digest-pinned cells as the PR lane, proves packaging
is digest-reproducible, and assembles a **draft** GitHub release — per-target
lean bundles with manifest and SBOM sidecars, a source archive, `SHA256SUMS`,
and notes rendered from `CHANGELOG.md` via
[docs/contributing/RELEASE_NOTES_TEMPLATE.md](docs/contributing/RELEASE_NOTES_TEMPLATE.md).
Publishing the draft is a human decision.

The lane takes its runtime digests, `ost` version, and per-cell verification
levels from `openstrata.ci.yaml` rather than restating them, so the PR and
release lanes cannot pin different runtimes. Run it manually
(`workflow_dispatch`) for a dry run that creates no release.

## Architecture

```text
SdfFileFormat
    -> PlyReader (tinyPLY adapter)
    -> GaussianPlyDecoder
    -> GaussianCloudData (gaussianCore; no USD types)
    -> GaussianLayerWriter
    -> UsdVolParticleField3DGaussianSplat
```

`gaussianCore` is an OpenStrata plain-library dependency described by
`libs/gaussian-core/openstrata.library.yaml`. OST builds and installs it into
the workspace prefix before configuring `gaussian-ply`; a plain root CMake
build uses the same in-tree target.

## PLY mapping

| PLY property | Interpretation | USD attribute |
| --- | --- | --- |
| `x`, `y`, `z` | local-space position | `positions` |
| `scale_0..2` | `exp(stored)` | `scales` |
| `rot_0..3` | scalar-first `(w,x,y,z)`, normalized | `orientations` |
| `opacity` | stable sigmoid | `opacities` |
| `f_dc_0..2` | RGB DC coefficient | `radiance:sphericalHarmonicsCoefficients[0]` |
| `f_rest_*` | channel-major RGB non-DC coefficients | remaining SH coefficients |

`f_rest_*` indices must be contiguous. With `R` rest scalars, `R / 3 + 1`
must be a square coefficient count: 1, 4, 9, 16, and so on. Unknown vertex
properties are ignored with an aggregated warning; the common `nx/ny/nz`
placeholders are recognized and ignored silently.

The generated stage contract is:

```text
/Asset                  Xform, kind=component, defaultPrim
    /Splat              ParticleField3DGaussianSplat
```

Layer metadata defaults to `upAxis = Y` and `metersPerUnit = 1`. These values
are importer policy because PLY does not standardize either field.

## Documentation

Start with the [documentation index](docs/README.md). Current behavior is in
the [capability matrix](docs/reference/CAPABILITY_MATRIX.md), the normative PLY
mapping is in [PLY_MAPPING.md](docs/reference/PLY_MAPPING.md), incomplete work
is in the [roadmap](docs/roadmap/), the versioned release sequence is in the
[release plan](docs/roadmap/release-plan.md), and measured OST usage is
recorded in the [dogfooding reports](docs/reports/ost/).

## License

Project code is Apache-2.0. tinyPLY's retained notice and fixed source revision
are documented in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
