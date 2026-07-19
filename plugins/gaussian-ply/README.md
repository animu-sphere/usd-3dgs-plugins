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
tests/                         C++ and Python integration coverage
tools/generate_fixtures.py     deterministic binary fixture generator
```

USD authoring is not here. It lives in the shared
[`libs/gaussian-usd`](../../libs/gaussian-usd/) library, which every format
bundle authors through; this bundle supplies only its own `GSPLY-****`
diagnostic codes to it.

The format is intentionally read-only. `WriteToFile` reports an unsupported
operation; `WriteToString` delegates to USDA so imported layers remain
inspectable by `usdcat` and OST's golden test.

## Implementation notes

- `Read()` authors the generated stage on a worker thread because Sdf reload
  runs the file format under an outer `SdfChangeBlock`; the change block is
  thread-local state, so the detached stage is authored unobserved and then
  transferred into the caller's layer. This is a correctness workaround, not a
  performance optimization — see the
  [design policy](../../docs/design/DESIGN_POLICY.md) §7.5.
- `Read(metadataOnly=true)` currently performs a full decode; header-only
  metadata reads are a planned improvement tracked in the
  [roadmap](../../docs/roadmap/backlog.md).

## Contracts and status

- [PLY mapping](../../docs/reference/PLY_MAPPING.md)
- [Capability matrix](../../docs/reference/CAPABILITY_MATRIX.md)
- [Supported configurations](../../docs/reference/SUPPORTED_CONFIGURATIONS.md)
- [Build and install guide](../../docs/guides/INSTALL.md)
- [Current roadmap](../../docs/roadmap/current.md)
