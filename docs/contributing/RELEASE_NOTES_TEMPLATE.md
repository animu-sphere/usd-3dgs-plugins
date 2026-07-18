# usd-3dgs-plugins {tag}

`gaussian-ply` opens canonical Gaussian Splatting PLY files directly as OpenUSD
layers and authors OpenUSD's standard `ParticleField3DGaussianSplat` schema.

- **Supported configurations:** [SUPPORTED_CONFIGURATIONS.md](https://github.com/animu-sphere/usd-3dgs-plugins/blob/{tag}/docs/reference/SUPPORTED_CONFIGURATIONS.md)
- **Capability matrix:** [CAPABILITY_MATRIX.md](https://github.com/animu-sphere/usd-3dgs-plugins/blob/{tag}/docs/reference/CAPABILITY_MATRIX.md)
- **PLY mapping:** [PLY_MAPPING.md](https://github.com/animu-sphere/usd-3dgs-plugins/blob/{tag}/docs/reference/PLY_MAPPING.md)
- **Install guide:** [INSTALL.md](https://github.com/animu-sphere/usd-3dgs-plugins/blob/{tag}/docs/guides/INSTALL.md)
- **Workspace contract:** [WORKSPACE.md](https://github.com/animu-sphere/usd-3dgs-plugins/blob/{tag}/docs/architecture/WORKSPACE.md)

{changelog}

## Artifacts

| Artifact | Contents |
| --- | --- |
| `gaussian-ply-{version}-<target>.tar.zst` | Target-specific read-only file-format plugin bundle and declared dependency closure |
| `gaussian-ply-{version}-<target>.manifest.json` | OpenStrata package manifest sidecar |
| `usd-3dgs-plugins-{version}-src.tar.gz` | Source archive at the tag |
| `SHA256SUMS` | SHA-256 checksums for every release artifact |

The binary target must match the host OpenUSD build, platform/compiler ABI, and
Python ABI. This project provides Gaussian USD data authoring, not a Hydra
renderer.

## SHA-256 checksums

```text
{checksums}
```

