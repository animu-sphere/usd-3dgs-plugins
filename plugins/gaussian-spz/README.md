# gaussian-spz — OpenUSD `spz` file-format plugin

Read-only import of Niantic [SPZ](https://github.com/nianticlabs/spz)
Gaussian Splatting assets. Scaffolded by
`ost plugin new usd-fileformat gaussian-spz --extension spz`.

**Development status (v0.3.0 cycle):** container reading, semantic decoding
into `GaussianCloudData`, and authoring through the shared `gaussianUsd` layer
writer are all implemented. Opening an `.spz` stage authors the same
`/Asset`/`/Asset/Splat` hierarchy as `gaussian-ply`; defective containers fail
with their stable `GSPZ-****` diagnostics. Remaining v0.3.0 work is a
redistributable SPZ corpus asset, PLY/SPZ equivalence fixtures, and
performance baselines.

Scope and format decisions are recorded in
[docs/reference/SPZ_FORMAT.md](../../docs/reference/SPZ_FORMAT.md): supported
container versions 1-3 (gzip), version 4 (ZSTD) deferred and rejected with a
specific unsupported-version diagnostic. The semantic mapping and the reference
quantization constants are in
[docs/reference/SPZ_MAPPING.md](../../docs/reference/SPZ_MAPPING.md).

## Layout

```
openstrata.plugin.yaml          bundle contract (identity, runtime range, provides, tests)
CMakeLists.txt                  builds the plugin library into lib/
cmake/OpenStrataPlugin.cmake    pinned, self-contained build/install mechanics
src/GaussianSpzFileFormat.{h,cpp}  thin SdfFileFormat integration (routes to gaussianUsd)
src/io/SpzReader.{h,cpp}        SPZ container reading (gzip framing, header/size
                                validation, quantized attribute spans)
src/io/GaussianSpzDecoder.{h,cpp}  semantic decode into GaussianCloudData
                                (dequantization, rotation decode, RUB→RDF)
src/io/GaussianSpzDiagnostics.h stable GSPZ-**** diagnostic identifiers
plugin/resources/gaussian-spz/  plugInfo.json + machine-readable diagnostics.json
tests/fixtures/                 deterministic valid + invalid container fixtures
tools/generate_fixtures.py      fixture generator (fixtures are committed)
```

The vendored DEFLATE/CRC32 implementation is `third_party/miniz` (MIT); the
gzip member framing is parsed by `SpzReader` itself so container diagnostics
can distinguish malformed, truncated, corrupt, and unsupported inputs.

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
