# Workspace contract

This is the binding structural contract for `usd-3dgs-plugins`. It fixes
component identities, dependency directions, root responsibilities, artifact
naming, and migration invariants. A structural change that contradicts this
document must change this document first.

Status: the initial workspace and `gaussian-ply` vertical slice are implemented.
Future component identities are reserved here but do not yet exist.

## 1. Components

| Identity | Kind | Status | Responsibility |
| --- | --- | --- | --- |
| `gaussian-ply` | OpenStrata plugin bundle (`usd-fileformat`) | implemented | Detect and decode canonical Gaussian Splatting PLY and author a standard OpenUSD Gaussian layer. |
| `gaussianCore` | plain CMake/OpenStrata static library | implemented | Format-independent Gaussian POD model, validation, scale/opacity/quaternion math, SH layout utilities, diagnostic message formatting, and the test-only model-contract checker every decoder is held to. |
| `gaussianUsd` | plain CMake/OpenStrata static library | implemented | Shared `GaussianCloudData` → OpenUSD schema authoring. Extracted from `gaussian-ply` in v0.3.0, at the moment design policy §7.4 reserves for it: the SPZ bundle would otherwise duplicate `GaussianLayerWriter`. Diagnostic codes stay owned by the calling bundle. |
| `gaussian-spz` | plugin bundle (`usd-fileformat`) | in progress — container reader implemented | Decode SPZ through `GaussianCloudData` and the shared authoring contract. |
| `gaussian-gltf` | plugin bundle or integration | undecided | Gaussian glTF/GLB support; identity is provisional until an ADR fixes ownership. |
| `gaussian-sog` | plugin bundle (`usd-fileformat`) | reserved | SOG decoding and, later, chunk/LOD composition. |

`gaussianCore` is not a plugin: it has no `plugInfo.json`, performs no plugin
registration, and exposes no OpenUSD types in its public API.

## 2. Dependency directions

Allowed today:

```text
gaussian-ply -> gaussianCore
gaussian-ply -> gaussianUsd
gaussian-ply -> tinyPLY (private, vendored parser implementation)
gaussian-spz -> gaussianCore
gaussian-spz -> miniz (private, vendored raw-DEFLATE/CRC32; gzip framing is in-repo)
gaussianUsd  -> gaussianCore
gaussianUsd  -> OpenUSD
```

Reserved future directions:

```text
gaussian-spz  -> gaussianUsd
gaussian-gltf -> gaussianCore
gaussian-sog  -> gaussianCore
any format bundle -> gaussianUsd
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
    thin SdfFileFormat integration (semantic decoding pending)

plugins/gaussian-spz/src/io/SpzReader.*
    SPZ container reading: gzip framing, header/size validation,
    quantized attribute spans; miniz isolation

libs/gaussian-core/
    format- and USD-independent Gaussian model/math

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
- future cross-format equivalence tests and aggregate packaging.

Plugin C++ sources, `plugInfo.json`, format fixtures, and private parser setup
belong to the bundle. Each bundle must remain buildable through `ost` as an
independent bundle; the root CMake build is an additional supported path.

## 5. Authored stage contract

Every decoder using the shared Gaussian authoring contract produces:

```text
/Asset                  Xform, kind=component, defaultPrim
    /Splat              ParticleField3DGaussianSplat
```

The initial layer policy is Y-up and one meter per unit. A source format that
contains authoritative axis/unit metadata may override the defaults only after
the mapping contract documents that behavior.

## 6. Artifact naming and versioning

Per-bundle artifacts use OpenStrata's target-qualified convention:

```text
gaussian-ply-<version>-<target>.tar.zst
gaussian-spz-<version>-<target>.tar.zst
gaussian-sog-<version>-<target>.tar.zst
usd-3dgs-plugins-<version>-<target>.tar.zst   # future aggregate
```

Until a real need for independent release cadences appears, every bundle and
plain library mirrors the repository-root `VERSION`. Git tags use `vX.Y.Z`.

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

`openstrata.ci.yaml` is the source of truth; the GitHub workflow is generated by
`ost ci generate github`. The declared PR matrix is:

| Host | Target | OST level |
| --- | --- | --- |
| Windows 2022 x86_64 | cy2026 / USD | L0-L4 |
| macOS 15 arm64 | cy2026 / USD | L0-L5 |
| Ubuntu 24.04 x86_64 | cy2026 / USD | L0-L5 |

Windows is capped at L4 until the hosted-runner multiline-USDA line-ending
behavior is resolved. Local Windows L5 currently passes.

The required local gate is:

```text
ost plugin build plugins/gaussian-ply
ctest (gaussianCore and gaussian-ply)
ost plugin test --workspace --up-to 5
ost plugin package plugins/gaussian-ply
ost plugin test plugins/gaussian-ply --from-package --up-to 5
ost ci validate
```

The packaged L5 golden is currently skipped because OST packages declared
fixtures but not the adjacent `.golden.usda`; this is recorded in the
[dogfooding report](../reports/ost/01-2026-07-18-v0.18.0-bootstrap.md).

## 9. Delivery status

| Milestone | Boundary | Status |
| --- | --- | --- |
| M0 | workspace, bundle, discovery, CI contract | implemented; hosted CI not yet observed |
| M1 | minimal ASCII/binary LE PLY read | implemented |
| M2 | Gaussian semantic mapping | implemented |
| M3 | validation and negative fixtures | implemented |
| M4 | integration, package, docs, notices | implemented locally; release gate incomplete |
| M5 | SPZ | implemented: container reader, semantic decoder, USD integration, real-asset corpus, performance baselines, and PLY/SPZ equivalence tests; release gate incomplete (hosted dry run, tag, draft) |

Current work and acceptance gaps are tracked in
[roadmap/current.md](../roadmap/current.md).

