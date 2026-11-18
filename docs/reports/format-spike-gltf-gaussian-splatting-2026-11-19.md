# Format spike: glTF `KHR_gaussian_splatting` — 2026-11-19

This is the FMT-09 evaluation in [current work](../roadmap/current.md).
It records what was observed on 2026-11-19 against the named upstream
revisions and files. It is not a support claim. The
[capability matrix](../reference/CAPABILITY_MATRIX.md) does not list glTF.

## Summary

| Check | Result |
| --- | --- |
| Specification | Ratified, normative prose under CC-BY-4.0 with a JSON schema. Every fact the decoder needs is stated in the text. No reference implementation is designated (§1, §2). |
| Attributes | Position, rotation, scale, opacity, and SH degree 0 are required. SH degrees 1-3 are optional and must be complete. Rotation, scale, and opacity may be quantized; SH is float32 only (§3). |
| Relation to the model | Values are already physical: linear scale, opacity in `[0, 1]`, unit quaternions. The SH basis, signs, and coefficient order match Graphdeco's exactly. Only the storage layout and the frame differ (§3, §4). |
| Coordinate frame | glTF is LUF (+X left, +Y up, +Z front), in metres. LUF→RUB is a 180° rotation about +Y: negate X and Z. It needs a sign table and no SH band rotation (§4). |
| Writers | SplatTransform since 2.7.0 writes conforming output with float32 attributes. Earlier versions wrote log-space scale, and versions before 2.0.0 wrote the PLY frame. The Huawei converter and JSplat copy their input frame unchanged. Cesium ion output is SPZ-compressed and uses a pre-ratification shape (§5). |
| Fixtures | No redistributable real-world file that uses only the base extension was found. Deterministic fixtures and fixtures derived from the CC0 corpus can be written with the Python standard library alone (§6). |
| Parser cost | No new third-party dependency. The work is the GLB container, a strict JSON parser (one already exists for SOG), base64 data URIs, resolver-backed external buffers, and accessor and buffer-view arithmetic (§7). |
| Ownership | Sdf picks a file format by extension and never calls `CanRead()`. Adobe's `usdGltf` already claims `gltf` and `glb` as primary for the `usd` target. FMT-10 must settle coexistence before any reader code lands (§8). |
| Recommendation | Keep glTF as the v0.12.0 target. No blocker was found. Scope as in §9. |

## 1. Sources

| Source | Revision | Licence |
| --- | --- | --- |
| [`KHR_gaussian_splatting`](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting) `README.md` and `schema/mesh.primitive.KHR_gaussian_splatting.schema.json` | KhronosGroup/glTF commit `521a91d491f0` (2026-10-13, "Rebase component types", #2677). Registry entry ratified in `81762cc328b1` (2026-09-03). | Text CC-BY-4.0; Khronos specification copyright terms |
| [glTF 2.0 specification](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/Specification.adoc) | commit `b0f245d0808d` (2026-11-18), which includes the "Binary Data Storage" update (#2641, merged 2026-09-04) that defines effective component types | Khronos specification copyright terms |
| [`KHR_gaussian_splatting_compression_spz_2`](https://github.com/KhronosGroup/glTF/pull/2531) | pull request, open on 2026-11-19 (last updated 2026-10-30) | — |
| [SplatTransform](https://github.com/playcanvas/splat-transform): `src/lib/writers/write-glb.ts`, `src/lib/readers/read-glb.ts`, `src/lib/utils/math.ts` | tag `v3.11.2`, commit `6f642939dd5d`; history of `write-glb.ts` from `6b4247f49505` | MIT |
| [3dgs_ply2gltf](https://github.com/NorbertNopper-Huawei/3dgs_ply2gltf) `main.cpp` | commit `4dc84650240d` (2026-07-16) | MIT |
| [JSplat](https://github.com/javagl/JSplat) `jsplat-io-gltf` | commit `808573c496af` (2026-07-09) | MIT |
| [CesiumJS](https://github.com/CesiumGS/cesium) `Specs/Data/Cesium3DTiles/GaussianSplats/{sh_unit_cube,tower}` | commit `4725bdf41ae4` (2026-11-17) | Apache-2.0 (repository) |
| [cesium-native](https://github.com/CesiumGS/cesium-native) `CesiumGltfReader` | commit `d23853ba9f84` (2026-11-17) | Apache-2.0 |
| [Adobe USD-Fileformat-plugins](https://github.com/adobe/USD-Fileformat-plugins) `gltf/src/` | commit `6441b97807a5` (2026-07-17) | Apache-2.0 |
| [OpenUSD](https://github.com/PixarAnimationStudios/OpenUSD) `pxr/usd/sdf/fileFormatRegistry.cpp`, `pxr/usd/sdf/layer.cpp` | `release` branch, commit `ee47c679abde` (2026-07-17) | Tomorrow Open Source Technology License |

The registry entry's status reads "Complete, Ratified by the Khronos Group".
The layout is documented, not inferred, which meets the
[format selection policy](../design/DESIGN_POLICY.md#4-format-selection).
The specification text is not copied into this repository. Only facts
restated in our own words are recorded here.

## 2. The extension object

The extension sits on a mesh primitive whose `mode` must be `POINTS` (0).
`KHR_gaussian_splatting` must be in `extensionsUsed`; listing it in
`extensionsRequired` is optional, because a viewer without the extension can
still draw the primitive as a point cloud.

| Property | Required | Values defined by the base extension |
| --- | --- | --- |
| `kernel` | yes | `"ellipse"` only |
| `colorSpace` | yes | `"srgb_rec709_display"`, `"lin_rec709_display"` |
| `projection` | no, default `"perspective"` | `"perspective"` only |
| `sortingMethod` | no, default `"cameraDistance"` | `"cameraDistance"` only |

- The schema accepts any string for each of these properties, so that future
  extensions can add values. A reader must therefore reject the values it
  does not know rather than rely on the schema.
- An extension that extends this one goes inside the
  `KHR_gaussian_splatting` object's own `extensions` and must also be in the
  top-level `extensionsUsed`. If it cannot fall back, it should also be in
  `extensionsRequired`. Compression extensions work this way.
- The material of the primitive, if any, must be ignored for splat
  rendering.

## 3. Attributes and accessors

| Attribute | Accessor type | Effective component types | Required |
| --- | --- | --- | --- |
| `POSITION` | VEC3 | as in core glTF: _float32_, or quantized types only through another extension such as `KHR_mesh_quantization` | yes |
| `KHR_gaussian_splatting:ROTATION` | VEC4 | _float32_, _snorm8_, _snorm16_ | yes |
| `KHR_gaussian_splatting:SCALE` | VEC3 | _float32_, _uint8_, _unorm8_, _uint16_, _unorm16_ | yes |
| `KHR_gaussian_splatting:OPACITY` | SCALAR | _float32_, _unorm8_, _unorm16_ | yes |
| `KHR_gaussian_splatting:SH_DEGREE_0_COEF_0` | VEC3 | _float32_ | yes |
| `KHR_gaussian_splatting:SH_DEGREE_l_COEF_n`, `l` = 1-3, `n` = 0..2l | VEC3 | _float32_ | a degree requires all lower degrees and all its own coefficients |

An effective component type is `componentType` together with `normalized`.
For example, _unorm8_ is `5121` with `normalized: true`, and _uint8_ is `5121`
without it. The core specification gives the dequantization: unsigned
normalized `c / (2^n - 1)`, signed normalized `max(c / (2^(n-1) - 1), -1)`.

Semantics compared with the [model contract](../reference/GAUSSIAN_MODEL_CONTRACT.md):

| Value | glTF storage | Model conversion |
| --- | --- | --- |
| Position | Local-space centre of the node that instantiates the mesh | Frame conversion only (§4) |
| Rotation | Unit quaternion in glTF order `(x, y, z, w)` | Reorder to scalar-first, then frame conversion. Quantized values are not exactly unit length, so they go through `NormalizeQuaternion`. |
| Scale | Linear standard deviation along the local axes. Must not be negative. | None. Linear already. |
| Opacity | Linear in `[0, 1]`. Values outside the range are invalid. | None. Already through sigmoid. |
| SH DC | RGB coefficient of `Y(0,0)`. Colour is `SH · 0.28209479 + 0.5`. | None. Same as Graphdeco `f_dc`. |
| SH rest | One VEC3 accessor per coefficient, ordered by `m` from `-l` to `l` within each degree | Transpose from attribute-major to Gaussian-major, then frame signs (§4) |

**The SH basis is Graphdeco's.** Appendix A of the extension lists the real
SH with the Condon–Shortley phase. Its degree 1-3 polynomials and signed
constants are the ones Graphdeco's renderer evaluates, in the same order:
`SH_DEGREE_1_COEF_0..2` correspond to the Graphdeco rest indices 0-2, degree
2 to 3-7, and degree 3 to 8-14. Coefficient index `i` therefore keeps its
[model meaning](../reference/GAUSSIAN_MODEL_CONTRACT.md#sh-ordering), and no
reordering within a band is needed. Only the storage layout changes: one
accessor per coefficient, each holding RGB.

**Rules a reader can check.** All attribute accessors of the primitive have
the same `count`. Scale is finite and not negative. Opacity is in `[0, 1]`.
The SH degrees present are contiguous from 0, and each degree is complete.
The model requires strictly positive scales, but glTF allows zero, and an
_unorm8_ code of 0 decodes to exactly 0. FMT-11 must choose between
rejecting zero and clamping it to a documented epsilon.

**Optional content.** `COLOR_0` is a fallback colour for viewers without the
extension. It is derived from the DC term and opacity and carries nothing
new. A point primitive may also have `indices`, which select and repeat
vertices. SplatTransform's reader accepts them.

## 4. Frame, units, and colour

**Frame.** The glTF 2.0 specification defines +Y up, the front of the asset
facing +Z, and the left side facing +X. That is LUF, right-handed. The
extension recommends training in that frame and says that converted data must
have positions, quaternions, and SH rotated on load.

LUF→RUB is `F = diag(-1, 1, -1)`, a proper rotation of 180° about +Y. As in
[ADR 0001](../adr/0001-model-frame-is-rub.md), it acts on each part of a
Gaussian:

- **Positions:** `(x, y, z) → (-x, y, -z)`.
- **Quaternions:** scalar-first `(w, x, y, z) → (w, -x, y, -z)`.
- **Scale, opacity, DC:** unchanged.
- **Rest SH:** each coefficient changes sign if its basis polynomial is odd
  under `x → -x, z → -z`. The table below was computed from the Graphdeco
  basis and checked by evaluating every polynomial at a mirrored point. The
  same script reproduces ADR 0001's Y/Z table.

  ```text
  band 1 (0-2):   +1, -1, -1
  band 2 (3-7):   -1, -1, +1, +1, +1
  band 3 (8-14):  +1, +1, +1, -1, -1, -1, -1
  ```

The shared `FlipYZAxes` helper does not cover this case. FMT-11 needs either
an X/Z variant or a general axis-sign helper. The general helper would also
support the axis-sign family of `SPZ_ADOBE_coordinate_system` that the
[SPZ v4 spike](format-spike-spz-v4-2026-11-17.md#8-proposed-scope-for-fmt-06-to-fmt-08)
deferred.

**Agreement with the PLY path.** SplatTransform keeps its data in a frame it
calls `IDENTITY`, and `Transform.PLY` is a 180° rotation about Z from it.
PLY's RDF rotated 180° about Z is LUF, so SplatTransform writes GLB in the
glTF frame. Converting a PLY to GLB with it, `(x, y, z) → (-x, -y, z)`, and
then importing with `F` gives `(x, -y, -z)`. That is exactly the RDF→RUB flip
that the PLY importer applies. A PLY and its SplatTransform GLB therefore
land in the same orientation, which the
[equivalence criterion](../reference/EQUIVALENCE.md) requires.

**Node transforms.** The extension places each Gaussian at the global
transform `G` of the node that instantiates the mesh. It requires `G` to
decompose into translation, rotation, and positive scale, and it says that SH
should rotate with `G`. In RUB the full mapping is
`F · G · p = (F G F⁻¹) · (F p)`. So the decoder can convert the local data
with `F` and author the conjugated transform `F G F⁻¹` as an `xformOp`,
without baking `G` into the data. Baking would need SH rotation by Wigner D
matrices for any rotation that is not axis-aligned. Cesium ion output uses a
non-identity node matrix that turns Z-up data into Y-up (§5). Ignoring node
transforms would therefore import that output on its side.

**Units.** glTF lengths are in metres. glTF would be the first supported
format that defines a length unit as well as an up axis. For glTF, the authored
`upAxis = "Y"` and `metersPerUnit = 1` would be source-derived claims rather
than [importer defaults](../design/DESIGN_POLICY.md#layer-metadata). The
mapping document should say so.

**Colour space.** The model and the authored layer have no colour-space
concept. Graphdeco training fits SH to sRGB-encoded images, so the data the
importers produce today is effectively `srgb_rec709_display`. That value can
be accepted unchanged. `lin_rec709_display` changes what the DC term means.
Authoring it unchanged would misrepresent the asset, so it should be rejected
with an *unsupported* diagnostic until the model can carry colour space.

## 5. Writers and readers

| Producer | Output | Evidence |
| --- | --- | --- |
| SplatTransform ≥ 2.7.0 | Conforming GLB: one primitive, one node, identity transform, float32 attributes, linear scale, sigmoid opacity, `COLOR_0` (_unorm8_), `colorSpace` `"srgb_rec709_display"`, glTF frame. Lists the extension in `extensionsUsed` only. `asset.generator` is `splat-transform <version>`. | `write-glb.ts` at `v3.11.2`. Linear scale since `a07f5aafce3f` ("Write linear-space SCALE in GLB export to follow KHR_gaussian_splatting spec"), first tagged in `v2.7.0`. |
| SplatTransform 2.0.0-2.6.x | As above, but SCALE holds log-space values | Same file between `829e4e368843` and `a07f5aafce3f` |
| SplatTransform 1.9.x | Log-space SCALE and the PLY frame, written unconverted | First writer, `6b4247f49505`, first tagged in `v1.9.0`. It has no frame conversion. |
| 3dgs_ply2gltf | `.gltf` with an external `.bin`, float32 only, linear scale, sigmoid opacity, `colorSpace` `"srgb_rec709_display"`. Lists the extension in both `extensionsUsed` and `extensionsRequired`. The frame is copied from the PLY unless `--convert` rotates Z-up data to Y-up. A Graphdeco RDF PLY is therefore written as RDF data in a file whose frame is LUF. | `main.cpp` |
| JSplat | float32 attributes, linear scale, opacity in `[0, 1]`. The caller passes `colorSpace`. Data is written in whatever frame the caller holds. Its reader bakes node global transforms. | `GltfSplatWriter.java`, `GltfSplatReader.java` |
| Cesium ion (as seen in CesiumJS test data) | `KHR_gaussian_splatting_compression_spz_2` listed in `extensionsRequired`, SH accessors typed VEC4, no `SH_DEGREE_0_COEF_0`, and no `kernel` or `colorSpace`. This is the pre-ratification shape. Data is Z-up, with a node matrix that rotates it to Y-up. | `sh_unit_cube/0/0.glb` (2,688 bytes, SHA-256 `55b3e768cc81164c5415da918b16954ab4600cf8116f5a94906c0acef3ea15b9`) and `tower/0/0.glb` (7,499,156 bytes, SHA-256 `9ef9d8d0756d1f423cd6aa8bd99f7dd26957f96df03dcbcec6e37103e39350b4`), JSON chunks inspected |

Readers: SplatTransform reads GLB since `v3.11.0`. It requires the
`"ellipse"` kernel, `"srgb_rec709_display"`, and the default `projection`
and `sortingMethod`. It also requires exactly one embedded buffer, exactly
one Gaussian primitive, and identity transforms on the Gaussian node and its
ancestors, and it rejects sparse accessors. CesiumJS and cesium-native read
the base extension and the SPZ compression extension. Adobe's `usdGltf`
contains no Gaussian support at the revision above.

Two writer outputs differ from the specification and contain nothing that
identifies them reliably:

- **Log-space scale** (SplatTransform before 2.7.0). Most log scales are
  negative, because splats are smaller than one unit, so the
  non-negative-scale rule rejects almost every such file. `asset.generator`
  names the version and could make the diagnostic specific.
- **PLY frame labelled as glTF** (SplatTransform 1.9.x, and 3dgs_ply2gltf
  without `--convert`). This is valid glTF that is upside down relative to
  its PLY source. As with SplatTransform 3.x SPZ files, the mapping
  reference can record the convention, but a reader cannot detect it.

## 6. Fixtures

- **Real assets.** The Khronos `glTF-Sample-Assets` repository has no
  Gaussian splat asset at `f0920f5b32d1` (2026-11-16). The only real files
  found are the CesiumJS test tiles. They are compressed and use the
  pre-ratification shape, and the provenance of `tower` is not stated, so
  they are not suitable for committing. Their JSON shape can be reproduced in
  a hand-written negative fixture: a required compression extension must
  fail with a specific diagnostic.
- **Deterministic.** A glTF writer needs only `json`, `struct`, and `base64`
  from the Python standard library. One generator can produce GLB, `.gltf`
  with an external `.bin`, and `.gltf` with a data URI. It can cover every
  quantized component type, SH degrees 0-3, byte strides, and the malformed
  cases in §9. No development-only dependency is needed, unlike `zstandard`
  for SPZ v4.
- **Derived from the CC0 corpus.** The committed `yashica-t4` and
  `leica-sofort` PLYs can be converted to GLB with the same generator, which
  applies the RDF→LUF rotation. This yields equivalence partners with
  recorded provenance, as the compressed PLY and SPZ corpora have.
- **Independent cross-check.** SplatTransform 2.7.0 or later is an
  independent conforming writer. Running it was not attempted, because npm
  installs are not permitted in this environment without the maintainer's
  approval. FMT-11 should ask for approval, or have the maintainer generate
  one file and record its version.

## 7. Parser cost

No third-party library is needed. The pieces are:

- **GLB container.** A 12-byte header (`glTF`, version 2, total length),
  then chunks with lengths that are multiples of 4. The JSON chunk comes
  first and occurs exactly once. At most one BIN chunk follows, and it must
  be second. Chunks of other types may appear and are skipped. The BIN chunk
  may be up to 3 bytes longer than `buffers[0].byteLength`.
- **JSON.** A Gaussian document is small: about 2.5 KB for SH degree 3 in
  the Cesium files. The strict parser in `gaussian-sog` (`SogJson`) already
  has the needed properties: no duplicate keys, no NaN, bounded depth and
  token count, and locale-independent numbers. A second user means moving it
  to a shared library or reusing it from `gaussian-sog`, and FMT-10 decides
  which. glTF needs its own size budget in
  [IMPORT_LIMITS.md](../reference/IMPORT_LIMITS.md).
- **Buffers.** There are three sources: the GLB BIN chunk; `data:` URIs with
  media type `application/octet-stream` or `application/gltf-buffer` and
  base64 payload (a small decoder); and relative paths. Relative paths may
  be percent-encoded and are resolved against the glTF file. They would go
  through `ArResolver::OpenAsset`, as SOG companions do
  ([RESOLVER_MATRIX.md](../reference/RESOLVER_MATRIX.md)).
- **Accessors.** Accessor elements come from the buffer view at
  `bufferView.byteOffset + accessor.byteOffset`, using `byteStride` when
  present. Buffer views may interleave attributes. Every range must be
  checked against the buffer length with overflow-checked arithmetic. Two
  legal forms add work: an accessor without a buffer view reads as all zeros,
  and sparse accessors replace selected elements. Both can be rejected as
  *unsupported* at first, as SplatTransform does for sparse accessors.
- **Metadata-only reads.** The count is the `POSITION` accessor's `count`,
  and the SH degree follows from the attribute names. Both come from the JSON
  alone. A metadata-only read therefore needs no buffer and no external
  `.bin`.
- **Binary size.** All of the above is new first-party code with no vendored
  library. The increase should be far smaller than the 100-230 KB that
  Zstandard added in v0.11.0. FMT-12 measures it.

## 8. Registration and coexistence

These are the facts FMT-10 has to decide against:

- `SdfLayer` chooses a file format with `SdfFileFormat::FindByExtension`,
  together with any `target` file-format argument. It never calls
  `CanRead()`. Whichever plugin wins `gltf` and `glb` opens every glTF file
  in the process, including mesh-only files that a Gaussian importer cannot
  read.
- When several formats claim one extension, `Sdf_FileFormatRegistry` uses
  the one whose plugin metadata has `"primary": true`. If none is primary, or
  more than one is, it raises a coding error and registers no format for that
  extension.
- Adobe's `usdGltf` declares `extensions: ["gltf", "glb"]`,
  `"primary": true`, and `"target": "usd"`. It also registers an
  `ArPackageResolver` for those extensions. If this repository's bundle
  declared `primary` too, both plugins would lose `.gltf` and `.glb` in any
  process that loads both. If it did not, Adobe's plugin would win whenever
  both are installed, and Gaussian files would import without their splats.
- OpenUSD itself ships no glTF file format.

The options include a dedicated, non-primary bundle documented as
incompatible with other glTF plugins in one process; a distinct `target`;
contributing Gaussian support to an existing glTF importer; or a
repository-specific extension. Each has costs for users and for the
[authored stage contract](../architecture/WORKSPACE.md#5-authored-stage-contract).
This report does not recommend one.

## 9. Proposed scope for FMT-10 to FMT-12

- **In scope.** `.glb` and `.gltf` with BIN-chunk, data-URI, and relative
  external buffers. The base extension with the `"ellipse"` kernel and
  `"srgb_rec709_display"`. Every effective component type in §3 for
  rotation, scale, and opacity. Float32 `POSITION`. SH degrees 0-3. Byte
  strides and interleaved buffer views. Full and metadata-only reads. LUF→RUB
  conversion in the decoder.
- **Decided by FMT-10.** The bundle and its registration (§8). Node
  transforms: authoring the conjugated `F G F⁻¹` as an `xformOp`, or
  rejecting non-identity transforms as SplatTransform does. Multiple Gaussian
  primitives, mesh instancing, and which scene is used. What happens to
  non-Gaussian meshes in the file: ignore with a warning, or reject. Whether
  the JSON parser moves to a shared library.
- **Rejected with specific diagnostics.** A required extension the reader
  does not implement, including every compression extension. An unknown
  `kernel` or `colorSpace`, and `"lin_rec709_display"`. A `mode` other than
  `POINTS`. Missing required attributes, unequal accessor counts, a
  component type outside §3, and an incomplete or non-contiguous SH degree.
  Negative or non-finite scale, opacity outside `[0, 1]`, and accessor or
  buffer-view ranges outside their buffer. Quantized `POSITION`
  (`KHR_mesh_quantization`), unless FMT-10 decides it is worth accepting. No
  observed writer emits it.
- **Ignored with a warning.** Unknown `projection` or `sortingMethod`
  values. These are rendering hints and do not change the stored data.
  `COLOR_0` and the material are ignored without a warning.
- **Open for FMT-11.** Zero scale (§3). Whether `indices` on the Gaussian
  primitive are supported. Whether to name the SplatTransform version in the
  log-scale diagnostic.
- **Package impact.** A new file-format plugin. FMT-10 decides whether it is
  a new bundle. No vendored component.

## 10. Not verified in this spike

- No glTF file from SplatTransform, 3dgs_ply2gltf, or JSplat was generated
  or examined. Their behaviour comes from source code.
- The colour-space claim in §4, that Graphdeco training data is effectively
  `srgb_rec709_display`, is reasoned from the training convention. It was
  not measured.
- The coexistence consequences in §8 were read from the OpenUSD and Adobe
  sources. They were not observed in a process that loads both plugins.
- The binary-size estimate in §7 is not measured.
