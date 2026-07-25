# gaussian-sog

An OpenUSD `SdfFileFormat` plugin that imports **SOG v2** — PlayCanvas's "Splat
Object Graphics" 3D Gaussian splat format — as a
`ParticleField3DGaussianSplat` stage.

Both SOG layouts are read, converging on one container reader and one semantic
decoder:

| Layout | What you open | Property planes |
| --- | --- | --- |
| bundled | `asset.sog` (a ZIP archive) | archived beside `meta.json` |
| unbundled | `meta.json` | companion `.webp` files in the same directory, loaded through the asset resolver |

```bash
usdcat asset.sog                    # bundled
usdcat path/to/meta.json            # unbundled
usdview asset.sog
```

The authored stage is identical in structure to a `gaussian-ply` or
`gaussian-spz` import — `/Asset` (`Xform`, `kind=component`, default prim) with
`/Asset/Splat` — because all three bundles author through the one shared
`GaussianLayerWriter`. Legacy SOG v1 is rejected with an unsupported-version
diagnostic, and streamed SOG (`lod-meta.json`, spatial chunks, LOD) is a later
milestone.

## How it decodes

- `src/io/SogReader.*` owns the container: layout detection, ZIP
  central-directory walking (vendored miniz), `meta.json` parsing and schema
  validation (`src/io/SogJson.*`), codebook checks, lossless-WebP plane decoding
  (vendored libwebp decoder subset), and plane dimension checks. A lossy plane
  is rejected rather than decoded approximately, because it would silently
  corrupt positions.
- `src/io/GaussianSogDecoder.*` owns the semantics: split-precision inverse-log
  positions, exponential scale codebooks, smallest-three quaternions, raw-DC and
  opacity decoding, and palette-resolved higher-order SH — into the shared
  `GaussianCloudData`. SOG stores PLY-native (Graphdeco RDF) columns, so the
  decoder applies the same shared `FlipYZAxes` conversion into the model's RUB
  frame that the PLY decoder applies
  ([ADR 0001](../../docs/adr/0001-model-frame-is-rub.md)).
- Every error and warning carries a stable `GSSOG-****` code, catalogued in
  [`plugin/resources/gaussian-sog/diagnostics.json`](plugin/resources/gaussian-sog/diagnostics.json)
  and cross-checked against the sources by the bundle's own test.

`GSSOG-E001` ("SOG import is not implemented") belonged to the v0.4.0 skeleton
and is retired, never reused.

## Reading `.json`

Because the stock unbundled layout is literally named `meta.json`, this plugin
registers the `.json` extension as well as `.sog`. That registration is broad,
so `CanRead()` is strict: a `.json` file is claimed only when a bounded prefix
parses as a JSON object with `version == 2` and the four required SOG property
descriptions, each with its `files` array. Unrelated JSON is declined; a
defective SOG `meta.json` past that gate still reaches `Read()` and fails with a
specific diagnostic rather than a silent routing refusal.

## Tests and fixtures

```bash
ost plugin build plugins/gaussian-sog
ost plugin test plugins/gaussian-sog          # L0-L5 pyramid
ctest --test-dir plugins/gaussian-sog/build/<target>   # reader, decoder, stage contract
python plugins/gaussian-sog/tools/generate_fixtures.py # regenerate fixtures
```

`tools/generate_fixtures.py` writes every fixture from source with no
third-party dependency: it contains a minimal lossless-WebP (VP8L) writer and
applies the reference encoder's own quantization equations. The positive
fixtures encode the decoder test kit's canonical clouds, so the decoder test can
require an exact `CompareClouds` round trip.

The format facts, dependency decisions, and fixture plan are recorded in
[docs/reference/SOG_FORMAT.md](../../docs/reference/SOG_FORMAT.md); the
normative semantic mapping is
[docs/reference/SOG_MAPPING.md](../../docs/reference/SOG_MAPPING.md); the model
the decoder targets is
[GAUSSIAN_MODEL_CONTRACT.md](../../docs/reference/GAUSSIAN_MODEL_CONTRACT.md).
