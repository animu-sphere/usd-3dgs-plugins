# Header and API boundary

This document defines the audience for installed headers and CMake targets,
the behavior consumers can observe, and which repository owns compatibility
commitments.

## Repository ownership

| Repository | Owns |
| --- | --- |
| `usd-3dgs-plugins` | Format readers and decoders, the shared semantic model and validation, USD layer authoring, diagnostics, and the packaging and verification of its plugin artifacts. |
| `usd-3dgs-runtime` | Integrated distribution and compatibility matrix, including consumer-facing runtime, API and ABI version guarantees when defined. This document grants no such integrated guarantee. |
| [`hydra-merlin`](https://github.com/animu-sphere/hydra-merlin) | GPU rendering and image conformance. The Formation owner records the verified host, stages, and component combination used in its integration runs. |

## Categories

| Tier | Audience | Installed? | Change policy |
| --- | --- | --- | --- |
| **Public behavior** | End users and applications opening or composing a USD stage. | The plugin binary and its `plugInfo.json`; no C++ header. | Changes to stage output, file-format arguments, or diagnostics need compatibility review. |
| **Contributor** | Developers adding or maintaining a decoder in this workspace or a fork. | Yes — `gaussianCore` and `gaussianUsd` contributor headers. | Deliberately reusable; changes update the [decoder guide](../contributing/ADDING_A_FORMAT_DECODER.md) and every in-repo decoder together. |
| **Internal (workspace)** | The two shared libraries and the bundles, within this repository. | Some headers are installed as a build necessity, not as an invitation. | No external promise. An external `#include` is unsupported. |
| **Plugin-private** | One bundle's own translation units. | No — lives under `plugins/*/src/`, never installed. | Free to change with no notice. |

`gaussianCore` and `gaussianUsd` expose a documented contributor API.
Installing those headers makes each bundle buildable standalone through
`ost`. Their installation provides no external C++ API or ABI guarantee.

## Observable plugin behavior

The current plugin contract covers:

- the **authored stage contract** — `/Asset` (Xform, `kind=component`,
  `defaultPrim`) with `/Asset/Splat`
  (`ParticleField3DGaussianSplat`), the attribute set and types, `upAxis`/
  `metersPerUnit`, and the `gs:*` custom data — fixed by
  [WORKSPACE.md §5](WORKSPACE.md) and authored identically for every format by
  the one shared writer;
- the **file-format arguments** (`shDegree`, `opacityThreshold`,
  `scaleMultiplier`), documented in
  [CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md);
- the **stable diagnostic codes** (`GSPLY-****`, `GSPZ-****`, `GSSOG-****`),
  each with a machine-readable catalog shipped in the bundle's resources and
  cross-checked by a test; codes are never renumbered or reused.

Review changes to these surfaces against existing stage and diagnostic tests
and document migration impact when needed. Integrated compatibility claims
belong to the runtime and must identify the component versions they cover.

## Header-by-header

### gaussianCore (`libs/gaussian-core/include/openstrata/gs/`)

Contributor API. USD-independent by contract
([WORKSPACE.md §2](WORKSPACE.md) forbids the `gaussianCore -> OpenUSD` edge).

| Header | Tier | Role |
| --- | --- | --- |
| `GaussianCloudData.h` | Contributor | The model every decoder targets; `kMaxShDegree`. |
| `GaussianMath.h` | Contributor | `Sigmoid`, `DecodeLogScale`, `NormalizeQuaternion`, `InferShDegree`, `FlipYZAxes`, `ComputeCloudExtent`, `ValidateGaussianCloud`. |
| `GaussianSizeMath.h` | Contributor | Overflow-checked size arithmetic and `TryResize` for the contract's allocation rule. |
| `GaussianImportStats.h` | Contributor | The shared per-import statistics record and formatter. |
| `GaussianDiagnostics.h` | Contributor | `FormatDiagnostic` — the one code/message join, so bracketed spelling cannot drift between bundles. |
| `GaussianFileIO.h` | Contributor | `PathFromUtf8`, `OpenInputFile` — the one way a reader opens a UTF-8 source path, so no bundle reintroduces a narrow-string open that Windows reads in the ANSI code page. |
| `testing/CloudContract.h` | Contributor (test-only) | The runtime-independent contract checker every decoder's tests run. |
| `testing/DecoderTestKit.h` | Contributor (test-only) | Canonical models, `CompareClouds`, invalid cases — a decoder tested without a USD stage. |
| `testing/Utf8TestPaths.h` | Contributor (test-only) | A fixture directory whose name no Windows ANSI code page can spell, for non-ASCII path tests. |

The `testing/` subdirectory is a naming convention, not a separate install:
those headers are header-only and used only from test executables. They are
installed with the rest but are never part of a shipped decode path.

### gaussianUsd (`libs/gaussian-usd/include/openstrata/gs/usd/`)

| Header | Tier | Role |
| --- | --- | --- |
| `GaussianLayerWriter.h` | Contributor | `GaussianCloudData` → the authored stage. Every bundle authors through this one class, which is what makes the public stage contract hold across formats by construction. Diagnostic codes stay owned by the calling bundle (`LayerWriterDiagnosticCodes`). |

This header depends on OpenUSD (`pxr/usd/sdf/layer.h`), so a contributor using
it inherits an OpenUSD build dependency; `gaussianCore` deliberately does not.

### Plugin-private (`plugins/*/src/`)

Never installed, no stability intent, free to change:

- `plugins/gaussian-ply/src/io/` — `PlyReader`, `GaussianPlyDecoder`,
  `GaussianPlyImportOptions`, `GaussianPlyDiagnostics`;
- `plugins/gaussian-spz/src/io/` — `SpzReader`, `GaussianSpzDecoder`,
  `GaussianSpzDiagnostics`;
- `plugins/gaussian-sog/src/io/` — `SogReader`, `GaussianSogDecoder`,
  `GaussianSogDiagnostics`;
- every `Gaussian*FileFormat.{h,cpp}` — the thin `SdfFileFormat` integration.

A bundle's `kSourceFormatToken` and diagnostic constants live here: shared
*within* the bundle (the file-format TU and the decoder), private *to* it.

## Rules this boundary imposes

1. A header installed by `gaussianCore` or `gaussianUsd` is a **Contributor**
   API; changing its signature updates
   [ADDING_A_FORMAT_DECODER.md](../contributing/ADDING_A_FORMAT_DECODER.md)
   and every in-repo decoder in the same PR.
2. `gaussianCore` never gains an OpenUSD include; a would-be shared helper that
   needs USD belongs in `gaussianUsd`.
3. A format bundle never installs a header. If two bundles need to share code,
   it moves into a `libs/` library with a deliberate tier, not into a
   cross-bundle `#include`.
4. Change observable behavior together with its reference and tests. Preserve
   existing diagnostic identifiers; coordinate integration changes with the
   runtime owner when they affect a published compatibility contract.
