# Workspace contract

This is the binding structural contract for `usd-3dgs-plugins`. It fixes
component identities, dependency directions, root responsibilities, artifact
naming, and migration invariants. A structural change that contradicts this
document must change this document first.

See the [capability matrix](../reference/CAPABILITY_MATRIX.md) for the formats
and variants these components support.

## 1. Components

| Identity | Kind | Responsibility |
| --- | --- | --- |
| `gaussian-ply` | OpenStrata plugin bundle (`usd-fileformat`) | Detect and decode Gaussian PLY. |
| `gaussian-spz` | plugin bundle (`usd-fileformat`) | Read SPZ containers and decode their quantized values. |
| `gaussian-sog` | plugin bundle (`usd-fileformat`) | Read SOG archives and resolve companion images, then decode their property planes. |
| `gaussian-splat` | plugin bundle (`usd-fileformat`) | Check the record layout of headerless `.splat` files and decode their 8-bit fields. |
| `gaussianCore` | plain CMake/OpenStrata static library | Own the Gaussian model, validation, shared math, diagnostics and statistics helpers, and the decoder test kit. |
| `gaussianUsd` | plain CMake/OpenStrata static library | Author the shared `GaussianCloudData` model into the OpenUSD schema. Diagnostic codes stay owned by the calling bundle. |

`gaussianCore` is not a plugin: it has no `plugInfo.json`, performs no plugin
registration, and exposes no OpenUSD types in its public API. The audience and
stability tier of every installed header is fixed by
[API_BOUNDARY.md](API_BOUNDARY.md); adding a decoder against those headers is
[ADDING_A_FORMAT_DECODER.md](../contributing/ADDING_A_FORMAT_DECODER.md).

## 2. Dependency directions

Allowed today:

```text
gaussian-ply -> gaussianCore
gaussian-ply -> gaussianUsd
gaussian-ply -> tinyPLY (private, vendored parser implementation)
gaussian-spz -> gaussianCore
gaussian-spz -> gaussianUsd
gaussian-spz -> miniz (private, vendored raw-DEFLATE/CRC32; gzip framing is in-repo)
gaussian-sog -> gaussianCore
gaussian-sog -> gaussianUsd
gaussian-sog -> miniz and libwebp (private, vendored ZIP and image decoders)
gaussian-splat -> gaussianCore
gaussian-splat -> gaussianUsd
gaussianUsd  -> gaussianCore
gaussianUsd  -> OpenUSD
```

Forbidden:

```text
gaussianCore -> any plugin bundle
gaussianCore -> tinyPLY
gaussianCore -> OpenUSD
gaussianUsd  -> any plugin bundle
gaussianUsd  -> tinyPLY or any other format parser
gaussian-ply -> another format bundle
any dependency cycle
```

The OpenStrata descriptor
`libs/gaussian-core/openstrata.library.yaml` gives the plain library a workspace
identity and CMake package/target. `gaussian-ply` declares the edge in
`requires.libraries`; `ost plugin build/test/package` resolves and executes it.

## 3. Source boundaries

```text
plugins/gaussian-ply/src/GaussianPlyFileFormat.*
    thin SdfFileFormat integration

plugins/gaussian-ply/src/io/PlyReader.*
    tinyPLY isolation and PLY scalar normalization

plugins/gaussian-ply/src/io/GaussianPlyDecoder.*
    Gaussian dialect validation and semantic decoding

plugins/gaussian-spz/src/GaussianSpzFileFormat.*
    thin SdfFileFormat integration

plugins/gaussian-spz/src/io/SpzReader.*
    SPZ container reading: gzip framing, header/size validation,
    quantized attribute spans; miniz isolation

plugins/gaussian-spz/src/io/GaussianSpzDecoder.*
    SPZ dequantization into GaussianCloudData

plugins/gaussian-sog/src/GaussianSogFileFormat.*
    thin SdfFileFormat integration; also supplies the resolver-backed
    companion loader for the unbundled layout

plugins/gaussian-sog/src/io/SogReader.*
    SOG container reading: layout detection, ZIP central directory,
    meta.json schema, lossless-WebP planes; miniz/libwebp isolation

plugins/gaussian-sog/src/io/SogJson.*
    strict JSON reader for meta.json, so a malformed document maps to a
    specific GSSOG diagnostic

plugins/gaussian-sog/src/io/GaussianSogDecoder.*
    SOG codebook/palette decoding into GaussianCloudData

plugins/gaussian-splat/src/GaussianSplatFileFormat.*
    thin SdfFileFormat integration

plugins/gaussian-splat/src/io/SplatReader.*
    .splat file layout: size, record count, import limit, and the
    signatures of files renamed from other formats

plugins/gaussian-splat/src/io/GaussianSplatDecoder.*
    .splat record decoding into GaussianCloudData

libs/gaussian-core/
    format- and USD-independent Gaussian model, math, size math,
    import-statistics record, and test-only contract checker/kit

libs/gaussian-usd/
    OpenUSD schema authoring, shared by every format bundle
```

The writer was extracted into `libs/gaussian-usd` in v0.3.0, when the SPZ
bundle became the second format consumer that would otherwise duplicate it.
The core remains USD-independent. Because both bundles author through one
implementation, the stage contract in §5 holds across formats by construction;
the format-independent model they both target is documented in
[GAUSSIAN_MODEL_CONTRACT.md](../reference/GAUSSIAN_MODEL_CONTRACT.md).

## 4. Root responsibilities

The repository root owns composition, not plugin implementation:

- dynamic discovery of `plugins/*` for plain CMake builds;
- workspace-wide version and OpenStrata platform/profile selection;
- `openstrata.ci.yaml` and generated CI;
- shared licensing, third-party notices, documentation, and release records;
- cross-format tests and aggregate packaging.

Plugin C++ sources, `plugInfo.json`, format fixtures, and private parser setup
belong to the bundle. Each bundle must remain buildable through `ost` as an
independent bundle; the root CMake build is an additional supported path.

The repository's `cmake/` directory holds CMake helpers shared by the two build
paths. Bundles and libraries include the helpers directly from their source
checkout, so configuring one of them as the top-level project works with the
same helpers. Target sources, decoder dependencies, OpenUSD components, and
bundle-specific resource and install declarations live with their targets.
Python CTest suites use the common runtime environment helper and name the
bundles they need. OpenStrata owns the generated `CMakeUserPresets.json` and
target toolchains under `.strata/`.

## 5. Authored stage contract

Every decoder using the shared Gaussian authoring contract produces:

```text
/Asset                  Xform, kind=component, defaultPrim
    /Splat              ParticleField3DGaussianSplat
```

The layer policy is Y-up and one meter per unit. A source format that
contains authoritative axis/unit metadata may override the defaults only after
the mapping contract documents that behavior.

The asset root carries `customData.gs.sourceFormat`, `gaussianCount`, and
`shDegree`. Native USD exports retain this import summary. The reference or
payload arc identifies the source layer. See the
[composition guide](../guides/COMPOSITION.md) for examples.

## 6. Artifact naming and versioning

Per-bundle artifacts use OpenStrata's target-qualified convention:

```text
gaussian-ply-<version>-<target>.tar.zst
gaussian-spz-<version>-<target>.tar.zst
gaussian-sog-<version>-<target>.tar.zst
gaussian-splat-<version>-<target>.tar.zst
```

The published aggregate is
`usd-3dgs-plugins-<version>-<target>-plugin-product.tar.zst`. See the
[installation guide](../guides/INSTALL.md) for the product layout.

Bundle versions follow the repository-root `VERSION`. The shared plain
libraries have their own project versions. Git tags use `vX.Y.Z`.

## 7. Change invariants

Every structural or format PR preserves these invariants:

1. Existing fixture stages remain semantically stable unless a documented
   contract change intentionally updates their goldens.
2. Format parsing never leaks into `gaussianCore` or the USD writer.
3. OpenUSD types never enter the public `gaussianCore` API.
4. A new format reaches USD through `GaussianCloudData`, not by copying PLY
   assumptions into its writer.
5. Manifest and CMake dependency declarations change together.
6. Plugin registration changes include a discovery test.
7. Third-party revision and license changes update both notices and package
   verification.

## 8. CI and verification contract

`openstrata.ci.yaml` is the source of truth. `scripts/generate_source_ci.py`
runs `ost ci generate github`. It adds the benchmark JSON to the workspace
jobs' evidence uploads. It also adds a path filter, so pull requests that
change only `docs/` or Markdown files skip the build matrix. The filter keeps
`THIRD_PARTY_NOTICES.md` because every bundle packages it. `docs.yml` checks
documentation on every pull request, and `clang-format.yml` runs only when
C/C++ sources or their format policy change. If a ruleset makes source CI
checks required, a skipped workflow leaves them pending. Change the filter
before enabling such a rule. The declared PR matrix is:

| Host | Target | OST level |
| --- | --- | --- |
| Windows 2022 x86_64 | cy2026 / USD | L0-L4 |
| macOS 15 arm64 | cy2026 / USD | L0-L5 |
| Ubuntu 24.04 x86_64 | cy2026 / USD | L0-L5 |

Windows is capped at L4 until the hosted-runner multiline-USDA line-ending
behavior is resolved. Local Windows L5 currently passes.

The bundle cells run OST's verification pyramid, which never reaches the
repository's own CTest suites. Three `kind: workspace` cells
(`workspace-ctest-pr-windows`, `-macos-arm64`, `-linux`) close that gap on the
same digest-pinned runtimes: `ost build` composes the root CMake tree and
`ost test` runs every suite in it — the gaussianCore and gaussianUsd unit
tests, each bundle's reader and decoder tests, the Python smoke and
asset-resolver tests, and the cross-format equivalence and
[benchmark corpus](../reference/BENCHMARK_CORPUS.md) tests that exist only
in the composed build. They upload CI evidence and publish no packages;
`scripts/release.py` skips them.

The required local gate is:

```text
ost plugin build plugins/gaussian-ply
ctest (gaussianCore and gaussian-ply)
ost plugin test --workspace --up-to 5
ost plugin package plugins/gaussian-ply
ost plugin test plugins/gaussian-ply --from-package --up-to 5
ost ci validate
```

See [supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md)
for the tested environments and [release records](../releases/) for
the gates exercised by a particular tag.

