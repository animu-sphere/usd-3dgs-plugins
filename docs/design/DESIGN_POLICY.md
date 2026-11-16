# Design and development policy

This document defines the product intent and engineering policy for
`usd-3dgs-plugins`. It governs format selection, semantic conversion, scene
authoring, and verification. Current behavior lives in
[reference/](../reference/); release scope and work order live in the
[roadmap](../roadmap/).

## 1. Project overview

`usd-3dgs-plugins` is an OpenStrata project that provides OpenUSD file-format
plugins for 3D Gaussian Splatting (3DGS) formats.

The repository identity is:

```text
animu-sphere/usd-3dgs-plugins
```

The OpenStrata workspace describes format bundles independently. Each decoder
targets the same Gaussian model and USD authoring contract. A dialect or
container revision may share its format family's bundle when their signatures
can be distinguished reliably.

## 2. Purpose

The project lets applications open 3DGS assets directly as OpenUSD layers
without requiring an external conversion step.

The governing principles are:

- implement each 3DGS format as an OpenUSD `SdfFileFormat` plugin;
- separate format decoding from USD authoring;
- share a format-independent Gaussian intermediate representation;
- author OpenUSD's standard Gaussian particle-field schema;
- keep import read-only; require a separate design for export (§13);
- optimize for correctness and testability before streaming performance.

OpenUSD's standard `ParticleField3DGaussianSplat` schema is used whenever
possible; a project-specific Gaussian schema may be introduced only when a
required concept cannot be represented by the standard schema.

Keep the import path renderer neutral. The
[API boundary](../architecture/API_BOUNDARY.md) defines ownership of
integrated runtime compatibility and rendering.

## 3. Naming

The repository is named `usd-3dgs-plugins` to distinguish 3D Gaussian
Splatting from Gaussian processes and general Gaussian volumes.

Bundle identities use format-oriented names:

```text
gaussian-ply
gaussian-spz
gaussian-sog
```

C++ uses short, stable namespaces rather than turning the repository name into
an identifier:

```cpp
openstrata::gs
openstrata::gs::ply
openstrata::gs::spz
openstrata::gs::sog
```

## 4. Format selection

Select formats by user demand, a public specification or independently
implementable reference, licensing and asset provenance, semantic fidelity,
and the ability to verify source and installed packages. Identify detectable
variants, coordinate and unit conventions, and any information the decoder
cannot recover. Require deterministic fixtures and equivalence checks at
tolerances derived from the encoding. Keep the work order in the
[release plan](../roadmap/release-plan.md).

### 4.1 glTF / GLB Gaussian Splatting

glTF also owns a scene graph, node transforms, buffers, accessors, and external
resources. An ADR must establish whether to integrate with a USD glTF
importer or provide a separate bundle, which Gaussian extensions and SPZ
interactions are in scope, and which standard scene data to author.

### 4.2 Streaming and composition

A format that describes chunks, streamed levels of detail, or multiple
objects requires an explicit USD composition design. Establish resolver
behavior, asset and chunk lifetime, and the loading model before adding
a parser for such layouts. Profile the need before adding streaming (§12).

## 5. Import path

Verify the complete path for a new format: a representative source asset
opens through `SdfLayer::FindOrOpen`, decodes into `GaussianCloudData`, and
authors the expected stage through the shared writer.

## 6. Scene graph policy

### `/Asset`

`/Asset` is the asset boundary and owns:

- `defaultPrim` identity;
- asset-level transforms and metadata;
- asset-level bounds, variants, and source-frame corrections where defined
  by the authored contract.

### `/Asset/Splat`

`/Asset/Splat` represents a Gaussian cloud as a single
`ParticleField3DGaussianSplat` prim. Additional hierarchy requires a concrete
use case and an update to the
[authored stage contract](../architecture/WORKSPACE.md#5-authored-stage-contract).

### Layer metadata

In the absence of authoritative source metadata, the defaults are:

```usda
(
    defaultPrim = "Asset"
    upAxis = "Y"
    metersPerUnit = 1
)
```

PLY does not standardize an up axis or linear unit. These values are importer
defaults, not source-derived claims, and must remain documented as such.

### Asset metadata

`/Asset` carries import provenance in `customData` — source format, Gaussian
count, and SH degree. Source-specific metadata beyond that is added only when
it is useful to downstream USD workflows.

### Extent

The authored extent is a conservative three-sigma maximum-scale bound.
Correctness matters more than tightness: a tighter rotated anisotropic
Gaussian AABB is worth implementing only if measurements show that loose
bounds materially affect viewport framing, culling, Hydra scene-index
performance, or large-scene traversal.

## 7. Architecture

The file-format entry point stays thin:

```text
SdfFileFormat
    -> format reader
    -> Gaussian semantic decoder
    -> GaussianCloudData
    -> USD Gaussian layer writer
```

### 7.1 PLY reader

The PLY reader isolates tinyPLY and owns:

- header parsing and element/property enumeration;
- vertex-count discovery;
- ASCII and binary decoding;
- property-buffer acquisition;
- normalization of PLY scalar types.

No file-format entry-point code calls tinyPLY directly.

### 7.2 Gaussian semantic decoder

The decoder interprets a PLY document as 3DGS data and owns:

- required-property and dialect validation;
- scale, opacity, quaternion, and spherical-harmonic conversion;
- SH-degree inference;
- non-finite-value checks;
- diagnostics.

Keep property naming rules in the semantic decoder. Any accepted aliases
must be explicit, documented, and covered by tests.

### 7.3 Gaussian intermediate representation

The shared representation is format-independent and uses standard C++ or
project POD types rather than USD types:

```cpp
struct GaussianCloudData {
    std::vector<Float3> positions;
    std::vector<Float3> scales;
    std::vector<Quaternion> rotations;
    std::vector<float> opacities;
    std::vector<Float3> dcCoefficients;
    std::vector<Float3> restCoefficients;
    int shDegree = 0;
    std::size_t gaussianCount = 0;
};
```

`gaussianCore` owns this model and its format-independent validation/math. It
does not expose OpenUSD types. Keep OpenUSD authoring in the shared writer
(§7.4).

### 7.4 USD Gaussian layer writer

The writer owns:

- prim and schema creation;
- attribute values and types;
- layer and asset metadata;
- `defaultPrim` and extent;
- writer-side diagnostics.

All format bundles must produce the same authored contract through this layer.

Keep the writer in `libs/gaussian-usd`. New decoders supply model values,
source metadata, and bundle-owned diagnostic codes to this library.

### 7.5 Worker-thread authoring workaround

`Read()` authors the generated stage on a worker thread
in the file-format entry points. Sdf reload executes `Read()`
under an outer `SdfChangeBlock`, and authoring a detached temporary stage on
the calling thread would observe that block. The change block is thread-local
state, so a worker thread authors the temporary stage unobserved; the
authored layer is then transferred into the caller's layer in one
`TransferContent` step, without a USDA serialization and reparse round-trip.

Treat the worker hop as a correctness workaround. Keep the reload path
covered by tests and re-evaluate detached authoring when upgrading OpenUSD.
The [backlog](../roadmap/backlog.md#performance-and-loading) records the
removal criteria.

### 7.6 `CanRead()` contract

`CanRead()` answers one question: *is this file plausibly one this bundle
handles?* It is a cheap routing decision, not a validation pass.

It must:

- check the file extension and the container's structural signature — for PLY,
  a readable header with the Gaussian property shape;
- read no more than the header, so it stays cheap on large assets;
- be `noexcept` in effect: any parse failure returns `false` rather than
  propagating.

It must **not**:

- decode or validate per-Gaussian data;
- guarantee that a subsequent `Read()` succeeds.

The consequence is deliberate and must stay documented for users:
**`CanRead()` returning `true` does not mean the asset is valid.** A file with
a well-formed header and truncated, non-finite, or internally inconsistent
body passes `CanRead()` and then fails `Read()` with a specific diagnostic.
That is the intended division of labor — `CanRead()` selects the plugin,
diagnostics explain the failure — and it is why a failing import reports a
`GSPLY-****`/`GSPZ-****` code rather than USD reporting no plugin was found.

The inverse is a stricter promise: `CanRead()` returning `false` means this
bundle will not attempt the file at all, so a format whose signature check is
too narrow silently disowns assets it could have read. New dialects are
therefore accepted by widening the signature deliberately, with a fixture, not
by loosening it until something loads.

## 8. PLY scope

See [PLY dialects](../reference/PLY_DIALECTS.md) for accepted encodings and
exporter behavior.

The canonical property set is:

```text
x, y, z
scale_0, scale_1, scale_2
rot_0, rot_1, rot_2, rot_3
opacity
f_dc_0, f_dc_1, f_dc_2
f_rest_*
```

The mapping is implemented in one semantic-decoder module and documented in
[PLY_MAPPING.md](../reference/PLY_MAPPING.md).

## 9. Value conversion

### Position

`x`, `y`, and `z` are used directly as local-space positions.

### Scale

Graphdeco-style `scale_*` values are logarithmic:

```text
actualScale = exp(storedScale)
```

USD stores the semantic scale rather than the source encoding.

### Opacity

The stored opacity is a logit:

```text
actualOpacity = sigmoid(storedOpacity)
```

USD stores the resulting value in `[0, 1]`.

### Rotation

`rot_0..3` is interpreted as scalar-first `(w, x, y, z)`, normalized after
loading. A near-zero quaternion becomes the identity with a warning. Non-finite
components are errors.

### Spherical harmonics

`f_dc_0..2` supplies the RGB DC coefficient. Contiguous `f_rest_*` values use
the Graphdeco channel-major layout and are reconstructed into RGB coefficients.
The total coefficient count must be a perfect square `(degree + 1)^2`; invalid
counts fail the load. Coefficients remain SH data and are not baked into RGB
color values.

## 10. Gaussian PLY detection

The `.ply` extension is shared with meshes and ordinary point clouds. The
reader therefore checks for a Gaussian-specific signature including:

```text
scale_0
rot_0
opacity
f_dc_0
```

A valid non-Gaussian PLY is rejected with a specific diagnostic:

```text
The file is a valid PLY file, but it is not a supported Gaussian Splatting PLY dialect.
```

Gaussian PLY detection must decline ordinary meshes and point clouds.

## 11. Strictness and error handling

The load fails when:

- the `vertex` element is missing or empty;
- `x`, `y`, or `z` is missing;
- a required Gaussian property is missing;
- counts or array lengths disagree;
- the file is malformed or truncated;
- a required property has an unsupported type;
- SH indices are missing, duplicated, non-contiguous, or form an invalid degree;
- a semantic value is non-finite or cannot be represented safely.

The load may continue with a warning when:

- a quaternion requires normalization;
- a zero-length quaternion is replaced with identity;
- unknown extra vertex properties are ignored;
- non-critical metadata cannot be interpreted.

## 12. Loading model and performance

Release temporary representations as the next stage of the pipeline takes
ownership. Measure both importer allocations and the resulting USD layer
when deciding whether an optimization reduces peak memory. Distinguish
whole-asset payload loading in USD from partial decoding within an asset.

### 12.1 Measure before redesigning

Performance decisions are based on measured bottlenecks, not assumptions.
Before any streaming or restructuring work, collect per-asset measurements:

- source file size and Gaussian count;
- `CanRead()` duration and full `Read()` duration;
- peak resident memory;
- generated USDC size;
- flattening duration;
- time until the stage becomes inspectable in `usdview`.

The measurement corpus should include a small synthetic fixture, a medium
real-world asset, and large references such as Mip-NeRF 360 `garden` and
`bicycle` — subject to the license and provenance review required for any
external asset (§17).

### 12.2 Preferred optimization order

1. Keep metadata-only reads independent of full decoding (§12.3).
2. Measure copying and allocation costs on the §12.1 corpus. Prefer reducing
   redundant copies and temporary storage before adding chunked or streaming
   import.

### 12.3 Metadata-only reads

`SdfFileFormat::Read(..., metadataOnly=true)` must not decode the full
Gaussian payload. A metadata-only read authors the `/Asset` and
`/Asset/Splat` structure plus whatever the header provides: Gaussian count,
SH degree, source format, default prim, up axis, and meters per unit. For
PLY, the count and SH degree are derivable from the vertex declaration and
the `f_rest_*` property declarations without loading vertex data.

## 13. Write support

The file-format plugins are read-only.

`USD Gaussian -> PLY` is a separate design problem because no single Gaussian
PLY write dialect is authoritative. `WriteToFile` returns unsupported with a
clear diagnostic. `WriteToString` may expose the imported layer as USDA for
inspection and testing; it is not PLY export.

An export proposal must define its lossless-versus-lossy behavior, supported SH degree,
coordinate and unit policy, opacity and scale encoding, unsupported-metadata
handling, and deterministic-output expectations before implementation.

## 14. Workspace structure

The [workspace contract](../architecture/WORKSPACE.md) owns the source
layout, dependencies, and packaging boundaries. Keep format code in its
bundle, USD-independent shared code in `gaussianCore`, and shared OpenUSD
authoring in `gaussianUsd`.

## 15. tinyPLY dependency policy

tinyPLY is vendored at an immutable commit. Floating dependency versions are
forbidden. An update must include:

- the exact upstream commit and version;
- retained upstream license text;
- updated `THIRD_PARTY_NOTICES.md` files;
- regenerated fixtures if parser behavior changes;
- parser, integration, OST, and package verification.

The current pin is recorded in the root third-party notice and capability
reference.

## 16. Test strategy

### Layer 1: parser and decoder unit tests

These minimize OpenUSD dependencies and cover property detection, missing and
invalid properties, scalar conversion, scale/opacity/quaternion transforms,
SH layout, malformed headers, truncated data, and non-Gaussian rejection.

### Layer 2: USD authoring tests

These inspect a small authored stage for `defaultPrim`, prim paths, schema and
attribute types, array lengths, metadata, extent, and numeric accuracy.

### Layer 3: file-format integration tests

These exercise real plugin discovery and `Usd.Stage.Open`/
`SdfLayer::FindOrOpen` against deterministic fixtures, including negative
fixtures.

### Layer 4: OpenStrata lifecycle tests

The supported lifecycle is:

```text
ost plugin inspect
ost plugin build
ost plugin doctor
ost plugin test --workspace --up-to 5
ost plugin package
ost plugin test --from-package
```

### Golden-test policy

Golden outputs validate stable stage semantics, not incidental formatting.
When a check is really about attribute values, prefer semantic assertions
over comparisons of full USDA text, which are brittle against formatting
changes.

## 17. Test data

Fixtures must be small, deterministic, redistributable, and hand-checkable.
Binary fixtures should be generated by a committed script. Real trained assets
may supplement the corpus only after license and provenance review; they must
never be the sole source of coverage.

## 18. Definition of success

The project is successful when:

- common 3DGS assets open as predictable OpenUSD stages;
- format-specific differences normalize into one common Gaussian model;
- generated data uses the standard OpenUSD Gaussian schema;
- plugin behavior is deterministic and well tested;
- compatibility boundaries are explicit;
- large datasets load within measured and documented resource limits;
- adding a new format does not require duplicating common math, validation,
  or USD authoring logic.
