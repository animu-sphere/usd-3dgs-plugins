# OpenUSD 3D Gaussian Splatting Plugins

[![CI](https://github.com/animu-sphere/usd-3dgs-plugins/actions/workflows/ost-source-ci.yml/badge.svg)](https://github.com/animu-sphere/usd-3dgs-plugins/actions/workflows/ost-source-ci.yml)
[![License: Apache-2.0](https://img.shields.io/github/license/animu-sphere/usd-3dgs-plugins)](LICENSE)

OpenUSD file-format plugins for read-only import of Gaussian PLY, SPZ, SOG, and
`.splat`.
They open trained splat assets as USD layers with
`ParticleField3DGaussianSplat` at `/Asset/Splat`. See the
[capability matrix](docs/reference/CAPABILITY_MATRIX.md) for supported variants
and limitations.

<p align="center">
  <img src="docs/assets/usd3dgs_with_hdparticlefield.gif" alt="Imported Gaussian PLY displayed in usdview" width="700" />
</p>

## Quick start

Download the archive for your host from the
[latest release](https://github.com/animu-sphere/usd-3dgs-plugins/releases/latest).
Check the [supported configurations](docs/reference/SUPPORTED_CONFIGURATIONS.md)
and follow the
[binary install guide](docs/guides/INSTALL.md#install-a-binary-release) to
verify and activate it. Then open a Gaussian PLY as a USD layer:

```sh
usdcat --flatten --out scene.usda scene.ply
```

Stock `usdview` can inspect the stage. To see splats, use a compatible Hydra
delegate such as OpenUSD's `hdParticleField` example, shown above. See the
[experimental Merlin PLY preview](docs/reference/CAPABILITY_MATRIX.md#hydra-rendering)
for a verified setup.

To build from source, follow the [build guide](docs/guides/BUILDING.md).
For references, payloads, instancing, and USDZ delivery, see the
[composition guide](docs/guides/COMPOSITION.md).

## Contributing and documentation

- [Documentation index](docs/)
- [Contributing](CONTRIBUTING.md)
- [Roadmap](docs/roadmap/) and [release history](docs/releases/)

## License

[Apache License 2.0](LICENSE). See [third-party notices](THIRD_PARTY_NOTICES.md)
for the licenses of vendored dependencies.
