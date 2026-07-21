# Adding a format decoder

How to add a Gaussian format to this repository. It generalizes the
PLY-specific loop in [CONTRIBUTING.md](../../CONTRIBUTING.md) to the shared
contract every decoder targets, so a third and fourth format are the same
shape as the two that shipped (`gaussian-ply`, `gaussian-spz`). The v0.4.0
Gaussian Import Foundation exists precisely so this guide can be followed
**without reading PLY or SPZ source** — only the documents it links.

If any step here forces you to copy code out of a bundle, or to teach the
writer about your format, stop: that is the seam this architecture keeps
closed, and the fix is a shared helper, not a per-bundle exception.

## The pipeline you are joining

```text
format reader → semantic decoder → GaussianCloudData → GaussianLayerWriter → ParticleField3DGaussianSplat
```

- **Reader** — container mechanics only: signatures, framing, integrity,
  producing raw typed fields. Format-specific, lives in your bundle,
  USD-free, no Gaussian semantics.
- **Semantic decoder** — undoes every source encoding and produces a
  `GaussianCloudData` in the shared contract. Format-specific, in your
  bundle, USD-free.
- **`GaussianCloudData`** — the format-independent model
  ([GAUSSIAN_MODEL_CONTRACT.md](../reference/GAUSSIAN_MODEL_CONTRACT.md)).
  The convergence point; nothing format-specific passes it.
- **`GaussianLayerWriter`** — the one shared writer (`libs/gaussian-usd`).
  You call it; you do not modify it and you do not write a parallel one.
- **Stage** — identical hierarchy, schema, and metadata for every format, by
  construction ([WORKSPACE.md §5](../architecture/WORKSPACE.md)).

The reader/decoder/diagnostics split and where each concern lives is fixed by
[DESIGN_POLICY.md §7](../design/DESIGN_POLICY.md) and the header tiers in
[API_BOUNDARY.md](../architecture/API_BOUNDARY.md).

## Before writing a decoder: the format decision doc

No undocumented binary layout is ever guessed
([DESIGN_POLICY.md §4](../design/DESIGN_POLICY.md)). Before code, land a
`docs/reference/<FORMAT>_FORMAT.md` recording — as
[SPZ_FORMAT.md](../reference/SPZ_FORMAT.md) and
[SOG_FORMAT.md](../reference/SOG_FORMAT.md) do — the specification source and
licence, which versions/variants are in scope, every new third-party
dependency pinned to a fixed revision with licence review
([THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)), the coordinate frame
and its conversion into the model's RUB frame
([ADR 0001](../adr/0001-model-frame-is-rub.md)), and the fixture plan. A new
production dependency or an authored-output change needs an ADR, not just this
doc.

## Steps

### 1. Scaffold the bundle

```sh
ost plugin new usd-fileformat gaussian-<fmt> --extension <ext> --dir plugins/gaussian-<fmt>
```

Then align it with the shipped bundles (compare against `plugins/gaussian-spz`):
set `plugin.version` and the CMake `project(... VERSION ...)` to the repo
`VERSION` (never hand-edit for a release — `scripts/release.py set-version`
owns it), declare `requires.libraries: gaussianCore` (and `gaussianUsd` once
you author), and add `openusd: ">=26.05,<27.0"`. The root `CMakeLists.txt`
discovers `plugins/*` by glob, so no root edit is needed.

The `gaussian-sog` skeleton is a worked example of exactly this state: a
registered bundle that builds, tests, packages, and rejects every file with a
stable not-implemented diagnostic, before any decoding exists.

### 2. Reader — container only (`src/io/<Fmt>Reader.*`)

Parse the container into raw typed fields. Keep USD and Gaussian semantics
out. Vendor a library only for pure container mechanics (decompression, ZIP,
image bitstreams), the way `tinyply` and `miniz` are vendored — never a
whole-format decoder, because a bool/empty-result API cannot express the
malformed / unsupported-version / internal diagnostic distinctions the release
requires. Compute every size with the overflow-checked helpers
(`openstrata/gs/GaussianSizeMath.h`) before allocating.

### 3. Decoder — into the shared contract (`src/io/Gaussian<Fmt>Decoder.*`)

Produce a `GaussianCloudData` obeying
[GAUSSIAN_MODEL_CONTRACT.md](../reference/GAUSSIAN_MODEL_CONTRACT.md). The
rules a new decoder most often trips:

- **Decoded physical values only.** No log-scales, no opacity logits, no
  quantized planes, no codebook indices reach the model — undo them first
  (§1).
- **Strictly positive linear scales; opacity already in `[0, 1]`** (§3-4).
- **Scalar-first normalized quaternions**, via the shared
  `NormalizeQuaternion` — not a private reimplementation (§3).
- **Gaussian-major SH**, DC separate from rest, `restCoefficients` length
  `N * ((D+1)^2 - 1)`. A format storing another order transposes here (§3).
- **Convert your source frame to RUB** with the shared `FlipYZAxes` where the
  conversion is the Y/Z negation — positions, quaternions, and SH together,
  never positions alone (§2, ADR 0001). A natively-RUB format converts
  nothing.
- **Supported degrees 0-3.** Reject a higher declared degree with your own
  *unsupported* (not malformed) diagnostic; never truncate silently (§3).
- **Overflow/allocation** through `GaussianSizeMath.h`; failure is a decode
  failure with your diagnostic, never a partial cloud (§3).
- End with `ValidateGaussianCloud` as a backstop — reaching it with invalid
  data means a decoder case was missed; a user is better served by your
  specific code.

Fill the optional [`GaussianImportStats`](../reference/GAUSSIAN_MODEL_CONTRACT.md)
record on the decode path (source format/version, counts, byte sizes,
read/decode timings) so instrumentation matches the other formats; the
file-format layer adds authoring time and bounds and emits it under a
`TF_DEBUG=GS<FMT>_IMPORT_STATS` flag.

### 4. Diagnostics (`src/io/Gaussian<Fmt>Diagnostics.h` + catalog)

Claim a `GSS<FMT>-****` / `GS<FMT>-****` namespace with the shared layout
convention (E0xx container/semantic, E1xx internal/USD authoring, E2xx entry
point, W0xx warnings). Every code defined in the header **must** appear in the
shipped `plugin/resources/gaussian-<fmt>/diagnostics.json`, and the bundle's
python test cross-checks the two in both directions. Codes are never
renumbered or reused once released. Use `FormatDiagnostic` so the bracketed
spelling matches every other bundle.

### 5. File format — thin integration (`src/Gaussian<Fmt>FileFormat.cpp`)

`CanRead` checks extension and container signature only, cheaply and
`noexcept` in effect — a true result is a routing decision, not a validity
promise ([DESIGN_POLICY.md §7.6](../design/DESIGN_POLICY.md)). `Read`
decodes, then authors through `GaussianLayerWriter`, passing your bundle's
diagnostic codes by name into `LayerWriterDiagnosticCodes` (a positional list
lets a swapped pair emit the wrong stable code silently). Author on the worker
thread and `TransferContent`, as the shipped bundles do
([DESIGN_POLICY.md §7.5](../design/DESIGN_POLICY.md)).

### 6. Tests

- **Decoder unit** (`tests/test_gaussian_<fmt>_decoder.cpp`) — hand-computed
  fixtures with exact expected values (SH *ordering* is invisible to
  validation and only a hand-checked fixture catches a transpose), plus
  `CheckCloudContract` on every valid fixture.
- **Decoder test kit** — encode the canonical models from
  `openstrata/gs/testing/DecoderTestKit.h` into your format, decode back, and
  require `CompareClouds` to return empty, at tolerances derived from your
  quantization. This is the fastest way to know positions, scales, rotations,
  opacity, every SH coefficient, the frame, and the extent are all correct at
  once, with no USD stage. `libs/gaussian-core/tests/test_decoder_kit.cpp`
  is a full worked example (a mock decoder using no bundle code).
- **Negative fixtures** — one per container diagnostic; the bundle test maps
  each malformed file to its expected code.
- **Integration** (`tests/test_gaussian_<fmt>_plugin.py`) — the stage
  contract (`/Asset`, `/Asset/Splat`, attributes, `upAxis`, custom data) and
  the catalog cross-check, mirroring the PLY/SPZ smoke tests.
- **Cross-format equivalence** — extend
  `tools/generate_equivalence_fixtures.py` to encode the one shared source
  model into your format too, so `tests/equivalence/` holds your format to
  PLY and SPZ at quantization-aware tolerances
  ([EQUIVALENCE.md](../reference/EQUIVALENCE.md)).

### 7. CI, packaging, docs

- Add three cells to `openstrata.ci.yaml` (one per hosted runner), then
  `ost ci generate github --force`. Nothing else in the workflow is
  hand-edited; `scripts/release.py` derives the release matrix from these
  cells. A bundle not yet ready to ship carries `publish: never` on its cells
  (as `gaussian-sog` does) and stays out of the release matrix until the
  marker is removed.
- Update [CAPABILITY_MATRIX.md](../reference/CAPABILITY_MATRIX.md) (a claim
  without a fixture is a bug), add `<FMT>_MAPPING.md` mirroring
  [SPZ_MAPPING.md](../reference/SPZ_MAPPING.md), add the component to
  [WORKSPACE.md](../architecture/WORKSPACE.md) §1-§2, record any new
  dependency in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md), and
  add a `CHANGELOG.md` entry.

## The local gate

```sh
ost plugin build plugins/gaussian-<fmt>
ctest --test-dir plugins/gaussian-<fmt>/build/<target>      # decoder + smoke
ost plugin test plugins/gaussian-<fmt> --up-to 5            # the pyramid
ost plugin package plugins/gaussian-<fmt>
ost plugin test plugins/gaussian-<fmt> --from-package --up-to 5
ost ci validate
```

The change invariants in [WORKSPACE.md §7](../architecture/WORKSPACE.md) — no
format parsing in the shared code, no OpenUSD in `gaussianCore`, manifest and
CMake edits together, a discovery test with every registration change — apply
to every decoder PR.
