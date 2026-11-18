# gaussian-splat — OpenUSD `splat` file-format plugin

Read-only import of headerless `.splat` Gaussian Splatting assets, the
fixed-record format written by the antimatter15 viewer, SplatTransform,
SuperSplat, gsplat.js, and Spark. Scaffolded by
`ost plugin new usd-fileformat gaussian-splat --extension splat`.

The bundle decodes `.splat` into `GaussianCloudData` and authors USD through
the shared `gaussianUsd` writer. See the
[format reference](../../docs/reference/SPLAT_FORMAT.md) for the record layout
and routing, and the [mapping](../../docs/reference/SPLAT_MAPPING.md) for
decoding and quantization loss.

## Layout

```
openstrata.plugin.yaml             bundle contract (identity, runtime range, provides, tests)
CMakeLists.txt                     builds the plugin library into lib/
src/GaussianSplatFileFormat.{h,cpp}  thin SdfFileFormat integration (routes to gaussianUsd)
src/io/SplatReader.{h,cpp}         file layout: size, record count, import limit,
                                   signatures of renamed files
src/io/GaussianSplatDecoder.{h,cpp}  semantic decode into GaussianCloudData
                                   (8-bit colour, alpha, rotation; RDF -> RUB)
src/io/GaussianSplatDiagnostics.h  stable GSSPLAT-**** diagnostic identifiers
plugin/resources/gaussian-splat/   plugInfo.json + machine-readable diagnostics.json
tests/fixtures/                    deterministic valid + invalid fixtures
                                   (installed with the package)
tests/reference/                   a third-party reference-written file (not packaged)
tests/corpus/                      CC0 corpus conversions (not packaged)
tools/generate_fixtures.py         fixture and corpus generator (outputs are committed)
```

Build and install settings use the workspace's
[OpenStrata CMake helper](../../cmake/OpenStrataPlugin.cmake). The bundle
vendors no third-party code.
