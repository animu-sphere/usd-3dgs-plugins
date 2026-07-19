# gaussianUsd

`gaussianUsd` owns the one path from `GaussianCloudData` into OpenUSD's
standard `ParticleField3DGaussianSplat` schema. It is described to OpenStrata
by `openstrata.library.yaml` and exported as the CMake target
`openstrata::gaussianUsd`.

It was extracted from `plugins/gaussian-ply/src/usd/` at the moment the design
policy reserves for it ([DESIGN_POLICY.md](../../docs/design/DESIGN_POLICY.md)
§7.4): when a second importer — SPZ, in v0.3.0 — would otherwise duplicate
`GaussianLayerWriter`. Because every format bundle authors through this one
class, the stage hierarchy, schema, metadata policy, stage metrics, and
default-prim behavior are identical across formats by construction rather than
by convention.

It owns:

- `/Asset` and `/Asset/Splat` prim and schema creation;
- attribute values and types;
- layer and asset metadata, `defaultPrim`, stage metrics, and extent;
- the metadata-only authoring path (design policy §12.3).

It deliberately does not own:

- diagnostic *codes*. The writer reports authoring failures through
  `LayerWriterDiagnosticCodes`, which the calling bundle supplies. One
  implementation, but a user sees the code of the format they actually
  imported (`GSPLY-E101` for PLY, `GSPZ-E101` for SPZ), and codes released by a
  bundle are never renumbered or reused.
- any format parser, `tinyPLY`, or format-specific compatibility logic;
- plugin registration or `plugInfo.json`;
- rendering or GPU upload.

Standalone build:

```sh
cmake -S libs/gaussian-usd -B libs/gaussian-usd/build/local \
      -DCMAKE_PREFIX_PATH=<your-openusd-install>
cmake --build libs/gaussian-usd/build/local
```

In the normal workspace flow, `ost plugin build plugins/gaussian-ply` resolves,
builds, and installs this library before configuring the plugin consumer.
