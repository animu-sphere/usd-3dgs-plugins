# gaussian-ply

Read-only OpenUSD `SdfFileFormat` for typical 3D Gaussian Splatting PLY files.
The bundle targets OpenUSD 26.05's `ParticleField3DGaussianSplat` schema and is
part of the parent OpenStrata workspace.

## Commands

```sh
ost plugin inspect plugins/gaussian-ply
ost plugin build plugins/gaussian-ply
ost plugin doctor plugins/gaussian-ply
ost plugin test plugins/gaussian-ply --up-to 5
ost plugin package plugins/gaussian-ply
```

## Source layout

```text
src/GaussianPlyFileFormat.*    thin USD file-format entry point
src/io/PlyReader.*             tinyPLY isolation and scalar conversion
src/io/GaussianPlyDecoder.*    Gaussian property mapping and validation
src/usd/GaussianLayerWriter.*  standard USD Gaussian schema authoring
tests/                         C++ and Python integration coverage
tools/generate_fixtures.py     deterministic binary fixture generator
```

The format is intentionally read-only. `WriteToFile` reports an unsupported
operation; `WriteToString` delegates to USDA so imported layers remain
inspectable by `usdcat` and OST's golden test.

## Contracts and status

- [PLY mapping](../../docs/reference/PLY_MAPPING.md)
- [Capability matrix](../../docs/reference/CAPABILITY_MATRIX.md)
- [Supported configurations](../../docs/reference/SUPPORTED_CONFIGURATIONS.md)
- [Build and install guide](../../docs/guides/INSTALL.md)
- [Current roadmap](../../docs/roadmap/current.md)
