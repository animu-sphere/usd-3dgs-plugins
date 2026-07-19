# Design and development policy

This document defines the product intent and engineering policy for
`usd-3dgs-plugins`. It is the source of truth for format priorities, semantic
conversion, scene authoring, validation, and milestone acceptance criteria.
Current implementation facts live in [reference/](../reference/); incomplete
work lives in [roadmap/](../roadmap/).

## 1. Project overview

`usd-3dgs-plugins` is an OpenStrata project that provides OpenUSD file-format
plugins for 3D Gaussian Splatting (3DGS) formats.

The repository identity is:

```text
animu-sphere/usd-3dgs-plugins
```

The project starts from OpenStrata's `usd-plugin-workspace` scaffold and adds
one independently described file-format bundle per source format. Gaussian
Splatting PLY is the reference implementation. SPZ, glTF/GLB Gaussian
Splatting, and SOG follow in that order unless evidence changes the priority.

## 2. Purpose

The project lets applications open 3DGS assets directly as OpenUSD layers
without requiring an external conversion step.

The governing principles are:

- implement each 3DGS format as an OpenUSD `SdfFileFormat` plugin;
- separate format decoding from USD authoring;
- share a format-independent Gaussian intermediate representation;
- author OpenUSD's standard Gaussian particle-field schema;
- keep early releases read-only;
- optimize for correctness and testability before streaming performance.

OpenUSD's standard `ParticleField3DGaussianSplat` schema is used whenever
possible; a project-specific Gaussian schema may be introduced only when a
required concept cannot be represented by the standard schema.

Rendering is outside the core responsibility of this repository. A file being
readable in `usdview` does not imply that the active Hydra renderer can
display Gaussian splats.

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

## 4. Format priority

Work proceeds in phases:

```text
Phase 1  stabilize Gaussian PLY
Phase 2  SPZ
Phase 3  expand interoperability: glTF/GLB Gaussian extensions, SOG,
         SPLAT, KSPLAT
Phase 4  advanced loading (chunked, reduced-copy, lazy, streaming)
Phase 5  optional export back to external formats
```

Phase 3 candidates are accepted individually, and only after their dialect,
ownership, specification status, and compatibility expectations are
documented. Phase 4 starts only after profiling confirms the need (§12).
Phase 5 stays out of scope until read interoperability is stable (§13).

### 4.1 PLY

PLY is the reference implementation. It establishes:

- the meaning of common 3DGS properties;
- the mapping to OpenUSD's Gaussian schema;
- the shared intermediate representation;
- deterministic fixture and expected-value conventions;
- the complete file-format plugin read path.

Phase 1 stabilization goals are:

- validate representative third-party exports;
- add metadata-only reads (§12.3);
- benchmark large scenes (§12.1);
- improve diagnostics;
- document supported PLY dialects, including verified Graphdeco reference
  datasets and SuperSplat-exported PLY behavior.

### 4.2 SPZ

SPZ is next because it is a compressed 3DGS-specific format that decodes into
nearly the same semantic model as PLY, which makes it the test of whether the
architecture genuinely supports reusable format-independent Gaussian data.
Its implementation must reuse the intermediate representation and USD
authoring layer and must add:

- compressed-format support;
- PLY-versus-SPZ semantic-equivalence tests;
- tolerance-aware comparisons for quantization error;
- comparative size and load-time benchmarks against PLY on production-sized
  assets.

Duplication of `GaussianLayerWriter` during this work triggers the
`gaussianUsd` extraction described in §7.4.

### 4.3 glTF / GLB Gaussian Splatting

glTF has a wider responsibility than a Gaussian decoder because it also owns a
scene graph, node transforms, buffers, accessors, and external resources. An
ADR is required before implementation and must decide:

- whether this is an independent file-format plugin or an integration with an
  existing USD glTF importer;
- whether only Gaussian extensions are supported;
- whether SPZ-compressed extensions are included;
- how much of the ordinary glTF scene graph is authored into USD.

### 4.4 SOG

SOG is strategically important for delivery, spatial chunks, LOD, and
streaming, but it also requires a USD composition design. Work proceeds in
four explicit steps:

```text
M1: read one SOG data object
M2: support bundle or multi-file layouts
M3: support streamed SOG and LOD
M4: map chunks to payloads, deferred loading, and spatial composition
```

## 5. Initial target: Gaussian PLY

The first product target is opening a Gaussian Splatting PLY directly as an
OpenUSD layer:

```cpp
SdfLayer::FindOrOpen("scene.ply");
```

The resulting scene graph is:

```text
/Asset
    /Splat
```

`/Asset` is the default prim. `/Asset/Splat` stores the Gaussian arrays.

## 6. Scene graph policy

### `/Asset`

`/Asset` is the asset boundary and owns:

- `defaultPrim` identity;
- asset-level transforms and metadata;
- future variants and coordinate-system corrections;
- future representations and asset-level bounds.

### `/Asset/Splat`

`/Asset/Splat` represents every Gaussian in the source PLY as one
`ParticleField3DGaussianSplat` prim. The initial implementation does not add
unnecessary intermediate scopes, and additional scopes or hierarchy require a
concrete use case such as multiple Gaussian clouds, LOD variants, animation,
source grouping, or renderer-specific material bindings. Future formats may
add sibling prims such as `/Asset/Cameras` or `/Asset/Metadata` when the
source format defines them.

### Layer metadata

The initial convention is:

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

Property naming policy is centralized here. The v0.1 implementation accepts
the canonical Graphdeco names; future aliases must be explicit, documented,
and tested here rather than spread through the file-format class.

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
does not expose OpenUSD types. The writer remains a separate layer; §7.4
defines when it moves into the shared `gaussianUsd` library.

### 7.4 USD Gaussian layer writer

The writer owns:

- prim and schema creation;
- attribute values and types;
- layer and asset metadata;
- `defaultPrim` and extent;
- writer-side diagnostics.

All format bundles must produce the same authored contract through this layer.

The writer is extracted into a shared `gaussianUsd` library
(`libs/gaussian-usd`) at a precise moment: when a second importer would
otherwise duplicate `GaussianLayerWriter`. It is not extracted before then.
The target structure after the second importer is:

```text
libs/
    gaussian-core/       # USD-independent model, math, and validation
    gaussian-usd/        # GaussianCloudData -> OpenUSD schema

plugins/
    gaussian-ply/
    gaussian-spz/
```

### 7.5 Worker-thread authoring workaround

`Read()` authors the generated stage on a worker thread
(`GaussianPlyFileFormat.cpp`). Sdf reload executes the file-format `Read()`
under an outer `SdfChangeBlock`, and authoring a detached temporary stage on
the calling thread would observe that block. The change block is thread-local
state, so a worker thread authors the temporary stage unobserved; the
authored layer is then transferred into the caller's layer in one
`TransferContent` step, without a USDA serialization and reparse round-trip.

The `std::async` hop is a correctness workaround, not a performance
optimization, and it must remain documented and tested. The avoided behavior
is reproduced on OpenUSD 26.05, the verified runtime. Every OpenUSD upgrade
must re-evaluate whether direct detached-stage authoring has become safe so
the workaround can be removed; the roadmap tracks this follow-up.

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

The required v0.1 encodings are:

```text
1. binary little-endian
2. ASCII
```

Binary big-endian is not part of the v0.1 support contract.

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

The v0.1 plugin is not a general mesh or point-cloud importer.

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

The implementation fully materializes the source. The conceptual path is:

```text
source file
    -> parser-owned property columns
    -> GaussianCloudData
    -> VtArray
    -> worker-authored SdfLayer
    -> TransferContent
```

Each representation is released as the next one is built: the parser's native
buffers are dropped column by column during float conversion, decoded columns
are freed as they move into `GaussianCloudData`, and the layer writer consumes
the cloud while authoring copy-on-write `VtArray` attributes. Peak memory is
therefore bounded by roughly one full-data representation plus the authored
layer, not by the sum of every stage. The one remaining overlap is the SH rest
pass, which briefly holds the remaining rest columns alongside the interleaved
rest array.

The implementation does not provide streaming, memory mapping, chunked
decoding, partial layer reads, lazy properties, GPU upload, USD caching, or
payload-level deferred loading.

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

1. Support metadata-only reads (§12.3).
2. Consider chunked or streaming import only after the copy reductions already
   in place — direct-to-`float` decoding, incremental release of parser and
   intermediate arrays, and direct layer authoring without a USDA round-trip —
   are measured on the §12.1 corpus.

### 12.3 Metadata-only reads

`SdfFileFormat::Read(..., metadataOnly=true)` must not decode the full
Gaussian payload. A metadata-only read authors the `/Asset` and
`/Asset/Splat` structure plus whatever the header provides: Gaussian count,
SH degree, source format, default prim, up axis, and meters per unit. For
PLY, the count and SH degree are derivable from the vertex declaration and
the `f_rest_*` property declarations without loading vertex data.

This is a high-priority improvement because it benefits asset inspection,
layer discovery, and tooling workflows. The current implementation ignores
`metadataOnly` and always performs a full read; the capability matrix records
this gap.

## 13. Write support

v0.1 is read-only:

```text
PLY -> USD layer
```

`USD Gaussian -> PLY` is a separate design problem because no single Gaussian
PLY write dialect is authoritative. `WriteToFile` returns unsupported with a
clear diagnostic. `WriteToString` may expose the imported layer as USDA for
inspection and testing; it is not PLY export.

Export stays out of scope until read interoperability is stable. Any future
exporter must define its lossless-versus-lossy behavior, supported SH degree,
coordinate and unit policy, opacity and scale encoding, unsupported-metadata
handling, and deterministic-output expectations before implementation.

## 14. Workspace structure

The binding structure is documented in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md). Its essential form
is:

```text
usd-3dgs-plugins/
├── openstrata.toml
├── openstrata.ci.yaml
├── CMakeLists.txt
├── plugins/
│   ├── gaussian-ply/
│   └── gaussian-spz/          # future
├── libs/
│   └── gaussian-core/
└── third_party/
    └── tinyply/
```

`gaussianCore` is a plain CMake/OpenStrata library, never a plugin bundle. Its
public surface is limited to the Gaussian model, validation, SH utilities, and
math used by multiple decoders.

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

The initial corpus covers one-Gaussian ASCII and binary little-endian files,
degree-1 SH layout, non-Gaussian mesh input, missing opacity, and truncated
binary input.

## 18. Milestones

| Milestone | Deliverable |
| --- | --- |
| M0 — workspace bootstrap | OpenStrata workspace, `gaussian-ply` bundle, discovery, build, and CI contract |
| M1 — minimal PLY read | tinyPLY, ASCII/binary LE, vertex position, `/Asset/Splat`, layer open |
| M2 — semantic mapping | scale, rotation, opacity, DC, and SH mapped to standard USD Gaussian attributes |
| M3 — validation | Gaussian detection, malformed/missing/count/non-finite diagnostics |
| M4 — integration quality | API inspection, OST L0–L5, CI matrix, package, mapping docs, notices |
| M5 — SPZ | SPZ decoder through the same model/writer, equivalence tests, measured performance |

Milestone status is historical evidence and therefore belongs in
[delivery-history.md](../reports/delivery-history.md), not in this policy.

## 19. v0.1 definition of done

v0.1 is ready to release when all of the following are evidenced:

- the repository is an OpenStrata plugin workspace;
- `gaussian-ply` builds and registers the `.ply` format;
- canonical Gaussian PLY opens as an `SdfLayer`;
- ASCII and binary little-endian inputs work;
- positions, semantic scales, normalized rotations, opacity, and SH are correct;
- `/Asset/Splat` and `/Asset` default prim follow the scene contract;
- non-Gaussian and malformed PLY inputs fail clearly;
- the plugin is read-only;
- unit, integration, workspace, and package-origin tests pass at their declared levels;
- tinyPLY licensing and an immutable revision are recorded;
- fixtures are deterministic and redistributable;
- a versioned package can be produced;
- the declared CI matrix has completed on every supported host.

## 20. Explicit v0.1 exclusions

- SPZ, SOG, glTF/GLB, SPLAT, and KSPLAT;
- USD-to-PLY export;
- a Hydra renderer or a guarantee of visible splats in stock `usdview`;
- GPU upload, compressed USD arrays, memory mapping, and lazy loading;
- animated Gaussians or multiple Gaussian clouds;
- camera and training-metadata import;
- user-configurable property mappings;
- arbitrary point-cloud or polygon-mesh PLY;
- automatic coordinate-system inference.

## 21. Implementation order

The initial vertical slice is intentionally ordered as:

```text
workspace and bundle scaffold
    -> discoverable empty plugin
    -> /Asset/Splat stage contract
    -> tinyPLY and positions
    -> scale, rotation, opacity
    -> spherical harmonics
    -> dialect detection and diagnostics
    -> deterministic fixtures and CI
    -> versioned package
```

The first proof is always the complete narrow path:

```text
PLY
    -> SdfFileFormat
    -> GaussianCloudData
    -> OpenUSD Gaussian prim
    -> SdfLayer::FindOrOpen succeeds
```

## 22. Final policy summary

```text
Repository:      animu-sphere/usd-3dgs-plugins
Framework:       OpenStrata usd-plugin-workspace
Initial bundle:  gaussian-ply
Parser:          tinyPLY, immutable vendored revision
Scene graph:     /Asset/Splat
Architecture:    reader -> semantic decoder -> GaussianCloudData -> USD writer
Roadmap:         PLY -> SPZ -> interoperability candidates (glTF/GLB, SOG,
                 SPLAT, KSPLAT) -> advanced loading -> optional export
v0.1:            read-only, fully loaded, ASCII + binary little-endian,
                 position + scale + rotation + opacity + SH
```

## 23. Definition of success

The project is successful when:

- common 3DGS assets open as predictable OpenUSD stages;
- format-specific differences normalize into one common Gaussian model;
- generated data uses the standard OpenUSD Gaussian schema;
- plugin behavior is deterministic and well tested;
- compatibility boundaries are explicit;
- large datasets load within measured and documented resource limits;
- adding a new format does not require duplicating common math, validation,
  or USD authoring logic.
