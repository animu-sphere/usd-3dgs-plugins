# gaussian-spz — OpenUSD `spz` file-format plugin

Read-only import of Niantic [SPZ](https://github.com/nianticlabs/spz)
Gaussian Splatting assets. Scaffolded by
`ost plugin new usd-fileformat gaussian-spz --extension spz`.

The bundle decodes SPZ into `GaussianCloudData` and authors USD through the
shared `gaussianUsd` writer. See the
[capability matrix](../../docs/reference/CAPABILITY_MATRIX.md#spz-input-and-semantic-mapping)
for supported versions and diagnostics, the
[format reference](../../docs/reference/SPZ_FORMAT.md) for container behavior,
and the [mapping](../../docs/reference/SPZ_MAPPING.md) for quantization.

## Layout

```
openstrata.plugin.yaml          bundle contract (identity, runtime range, provides, tests)
CMakeLists.txt                  builds the plugin library into lib/
src/GaussianSpzFileFormat.{h,cpp}  thin SdfFileFormat integration (routes to gaussianUsd)
src/io/SpzReader.{h,cpp}        SPZ container reading (v1-v3 gzip framing, v4
                                TOC and zstd streams, header/size validation,
                                extension records, quantized attribute spans)
src/io/GaussianSpzDecoder.{h,cpp}  semantic decode into GaussianCloudData
                                (dequantization, rotation decode; native RUB)
src/io/GaussianSpzDiagnostics.h stable GSPZ-**** diagnostic identifiers
plugin/resources/gaussian-spz/  plugInfo.json + machine-readable diagnostics.json
tests/fixtures/                 deterministic valid + invalid container fixtures
                                (installed with the package)
tests/reference/                third-party reference-written files (not packaged)
tests/corpus/                   real-asset subsets (not packaged)
tools/generate_fixtures.py      fixture generator (fixtures are committed); the
                                v4 fixtures need the Python `zstandard` package
```

Build and install settings use the workspace's
[OpenStrata CMake helper](../../cmake/OpenStrataPlugin.cmake).

The vendored DEFLATE/CRC32 implementation is `third_party/miniz` (MIT), and
the vendored zstd frame decoder for version 4 is `third_party/zstd`
(BSD-3-Clause, decompression only). The gzip member framing and the v4 table
of contents are parsed by `SpzReader` itself so container diagnostics can
distinguish malformed, truncated, corrupt, and unsupported inputs.

## Workflow

```sh
ost plugin inspect plugins/gaussian-spz   # Level 0: bundle structure
ost plugin build plugins/gaussian-spz     # build the shared library into lib/
ost plugin doctor plugins/gaussian-spz    # staged diagnostics
ctest --test-dir build/<target> -R gaussianSpz  # reader, decoder, and smoke tests
```

The copied CMake helper is versioned with this scaffold and requires neither an
OpenStrata checkout nor `ost` at build time. Keep bundle-specific targets,
components, resources, and tests in `CMakeLists.txt`; update helper mechanics by
reviewing a newer template rather than linking to the generator source tree.
