# Header and API boundary

This classifies every installed header and CMake target so that "installed"
is never mistaken for "public and stable". It is the v0.4.0 deliverable
(design policy §7.4; [roadmap/current.md](../roadmap/current.md) workstream 6)
that a header's audience is a deliberate decision, recorded here, not an
accident of which `install()` rule happened to copy it.

**No ABI or API stability is promised before v1.0.0.** Every category below
may change between minor releases in this pre-1.0 window; the categories say
*who a header is for* and *what breaks if it changes*, not that anything is
frozen. The v1.0.0 stability commitments in the
[release plan](../roadmap/release-plan.md#v100--stable-import-api-and-file-format-support)
are declared against the **public** tier defined here, and only against it.

## Categories

| Tier | Audience | Installed? | Stability intent |
| --- | --- | --- | --- |
| **Public** | End users composing a USD stage — they never `#include` anything; the surface is the authored stage and the file-format arguments. | The plugin binary and its `plugInfo.json`; no C++ header. | The stage contract and diagnostics are the compatibility surface (see below). Frozen at v1.0.0. |
| **Contributor** | A new format decoder inside this repository (or a downstream fork adding one). | Yes — `gaussianCore` and `gaussianUsd` public headers. | Deliberately reusable, documented in [ADDING_A_FORMAT_DECODER.md](../contributing/ADDING_A_FORMAT_DECODER.md). May change pre-1.0; changes update that guide and every in-repo decoder together. |
| **Internal (workspace)** | The two shared libraries and the bundles, within this repository. | Some headers are installed as a build necessity, not as an invitation. | No external promise. An external `#include` is unsupported. |
| **Plugin-private** | One bundle's own translation units. | No — lives under `plugins/*/src/`, never installed. | Free to change with no notice. |

The load-bearing distinction is **Contributor vs. Public**. `gaussianCore`
and `gaussianUsd` headers are installed and are a real, documented API — but
their audience is *someone writing a decoder in this workspace*, not an
arbitrary external consumer, and not the end user. Installing them is what
makes a second bundle buildable standalone through `ost`; it is not a
statement that the Gaussian model struct is a frozen public type.

## The public surface is the stage, not a header

The end-user compatibility surface has no `#include`:

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

These are what v1.0.0's "breaking prim-path or file-format-argument changes
require a major version" commitment binds. A C++ caller linking `gaussianCore`
is a contributor, covered by the tier above, not by that commitment.

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
| `testing/CloudContract.h` | Contributor (test-only) | The runtime-independent contract checker every decoder's tests run. |
| `testing/DecoderTestKit.h` | Contributor (test-only) | Canonical models, `CompareClouds`, invalid cases — a decoder tested without a USD stage. |

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
- `plugins/gaussian-sog/src/io/` — `GaussianSogDiagnostics` (the v0.5.0
  reader/decoder join it);
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
4. The end-user promise is the stage, the arguments, and the diagnostics — the
   things v1.0.0 will freeze. A C++ signature is not that promise before
   v1.0.0, and this document is where any change to that stance is recorded
   first.
