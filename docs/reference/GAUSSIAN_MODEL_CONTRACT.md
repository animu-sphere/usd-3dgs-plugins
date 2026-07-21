# Gaussian model contract

This is the normative contract for `GaussianCloudData`, the format-independent
representation every decoder targets
(`libs/gaussian-core/include/openstrata/gs/GaussianCloudData.h`). Two shipped
decoders — `gaussian-ply` (v0.1.0) and `gaussian-spz` (v0.3.0) — produce this
model, and `gaussianUsd` authors either one without knowing which format it
came from.

This revision's purpose (v0.4.0, design policy §7.4) is that the *next*
decoder can be written against this document alone: satisfying the contract
must not require reading PLY or SPZ code. Where this document and a decoder
disagree, the decoder is wrong.

The format-specific halves of each mapping live with their formats
([PLY_MAPPING.md](PLY_MAPPING.md), [SPZ_MAPPING.md](SPZ_MAPPING.md)); this
document owns only what is true for every format.

A decoder that satisfies this contract is guaranteed to author a valid stage.
A decoder that violates it fails in `ValidateGaussianCloud` before any USD
object is constructed, not in the middle of authoring.

## 1. What belongs in the model

The model holds **decoded physical values**, never source encodings. Every
format-specific encoding is undone by the decoder before the data enters the
model:

| Source encoding | Model value |
| --- | --- |
| Graphdeco log-scales | linear scale |
| Graphdeco opacity logits | opacity in `[0, 1]` |
| SPZ fixed-point positions | linear position |
| SPZ 8-bit log-encoded scales | linear scale |
| SPZ smallest-three rotations | normalized quaternion |
| SPZ quantized SH coefficients | dequantized coefficients |
| SOG WebP image planes | linear position, scale, rotation, opacity |
| SOG codebook indices and palette labels | dequantized SH coefficients |

The SOG rows are stated now, before a SOG decoder exists, because they are the
kind of representation this rule exists to keep out: WebP pixels, codebook
indices, and palette labels are all *encodings of* Gaussian parameters, and
every one of them is resolved to the physical value during decoding.

The corollary is that the model has no quantization concept, no
"is this format compressed" flag, and no per-format branches. Anything a
consumer would have to know the source format to interpret is a contract
violation.

## 2. Coordinate system, axes, and units

**The model carries source-native units and defines exactly one reference
frame. A decoder never *infers* a frame from the data; it applies the fixed
conversion its format's documented convention requires, or none.** Automatic
coordinate-system inference — deducing an asset's orientation by inspecting
it — remains an explicit non-goal
([DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §20). A per-format constant
conversion is not inference.

Positions are local-space centers in the source asset's own units. The authored
`upAxis = "Y"` and `metersPerUnit = 1` are **importer defaults, not
source-derived claims** (design policy, *Layer metadata*), and must keep being
documented as such.

This has a consequence a second format made concrete. Graphdeco PLY is
conventionally right-down-front (RDF, Y-down); SPZ conventionally stores
right-up-back (RUB, Y-up). Passing both through unconverted would put two
equivalent assets in different orientations on the same stage, which the
cross-format [equivalence criterion](EQUIVALENCE.md) forbids.

Therefore:

- **The model has exactly one reference frame, and every decoder whose format
  defines a different conventional frame converts into it** — positions,
  quaternions, and SH coefficients together, never positions alone — and
  documents the conversion in its own mapping document. For SPZ this is the
  RUB→RDF axis flip ([SPZ_MAPPING.md](SPZ_MAPPING.md)).
- **The reference frame today is the PLY-native RDF frame**, the frame v0.1.0
  authored first. Which frame the model *should* define — including whether
  the current mismatch between the RDF model frame and the authored
  `upAxis = "Y"` declaration is corrected while the project is pre-1.0 — is
  the subject of the coordinate-system ADR, a v0.4.0 deliverable
  ([current.md](../roadmap/current.md)). When that ADR lands, its decision
  supersedes this paragraph and this section will cite it.

## 3. Field-by-field contract

For a cloud with `N = gaussianCount` Gaussians and SH degree `D`:

| Field | Length | Meaning and admissible values |
| --- | --- | --- |
| `positions` | `N` | Local-space centers, decoded physical values. Finite. |
| `scales` | `N` | Per-axis standard deviations, **linear, strictly positive**. Finite. Not log-encoded. |
| `rotations` | `N` | Orientation, **scalar-first** `(real, i, j, k)`, normalized. Finite. |
| `opacities` | `N` | **In `[0, 1]`**, already through sigmoid. Finite. Not a logit. |
| `dcCoefficients` | `N` | RGB SH DC term, one triple per Gaussian. Finite. |
| `restCoefficients` | `N * ((D+1)^2 - 1)` | Non-DC RGB SH terms. Finite. |
| `shDegree` | — | `0 <= D <= 3`, consistent with the array lengths above. |
| `gaussianCount` | — | `N > 0`. An empty cloud is an error, not an empty stage. |

**Every field is required.** The model defines no optional arrays: a decoder
that cannot produce one of these fields for a well-formed input does not have
a partial model, it has a decode failure. The one admissible empty array is
`restCoefficients`, which has length zero exactly when `D = 0`; that is the
length formula, not an optional field.

`CoefficientsPerGaussian()` returns `(D+1)^2` and **includes the DC term**, so
`restCoefficients` holds one fewer triple per Gaussian than that.

### Supported SH degrees

Supported degrees are **0 through 3**, the Graphdeco training convention's
range and the maximum every targeted format (PLY, SPZ, SOG v2) can carry. A
source asset declaring a higher degree is rejected by the decoder with its own
diagnostic; it is not truncated to degree 3, because silently discarding
authored coefficients would misrepresent the asset. Truncation happens only
when the *user* asks for it (the PLY `shDegree` import option), and an
explicit cap below the source degree is not a contract violation — the decoded
model simply is a degree-`min(cap, source)` cloud.

### Quaternion order

`Quaternion` is scalar-first (`real, i, j, k`). This matches the Graphdeco
`rot_0..rot_3` convention directly and maps to `GfQuatf(real, i, j, k)`
unchanged. A format storing vector-first, or storing only the smallest three
components as SPZ does, reorders during decoding — never at the model boundary
and never in the writer.

Decoders normalize, using the shared `NormalizeQuaternion` helper rather than
a private reimplementation. A zero-length or near-zero quaternion is replaced
with identity and reported as a warning rather than failing the load; a
non-finite quaternion is a decode failure. Quaternions `q` and `-q` denote the
same rotation and both are admissible; a decoder is not required to pick a
canonical sign.

### SH ordering

`restCoefficients` is ordered **by Gaussian, then by coefficient**, with each
element an RGB triple:

```text
index = gaussian * ((D+1)^2 - 1) + coefficient
```

This is Gaussian-major. Formats that store coefficients channel-major
(Graphdeco PLY) or attribute-major (SPZ) transpose during decoding. The writer
interleaves DC and rest into the single authored
`radianceSphericalHarmonicsCoefficients` array, DC first per Gaussian.

Within one Gaussian, coefficient index `i` means what the Graphdeco rest
index `i` means: bands in ascending degree, in the order Graphdeco training
exports write them ([PLY_MAPPING.md](PLY_MAPPING.md)). A format that stores
another order converts during decoding; the bundle proves its ordering
against a fixture with hand-computed coefficients, because ordering mistakes
preserve array lengths and are invisible to shared validation.

### Maximum count and overflow

The model imposes no count ceiling of its own below what the address space
admits; real ceilings are container facts and stay format-specific (SPZ caps
`pointCount` at `2^31 - 1` per its reader; a PLY vertex count must fit
`size_t`). What the contract requires of every decoder:

- Every size derived from `N` and `D` — element counts, the
  `N * ((D+1)^2 - 1)` rest length, and allocation byte sizes — is computed
  with overflow-checked arithmetic *before* any allocation.
- A count whose derived sizes overflow, or whose allocation fails, is a decode
  failure with the format's own diagnostic. Clamping the count, loading a
  prefix, or dropping arrays to fit are all forbidden — a partial cloud never
  reaches the model.
- `N = 0` is likewise a decode failure. Decoders should reject it at the
  container level with a specific diagnostic (PLY: empty vertex element); the
  shared gate rejects it regardless.

### Retained source metadata

The model's fields are exhaustive: **it retains no source metadata**. Facts
about the source that do not change how a consumer interprets the arrays
travel *beside* the model, never in it:

- the source-format token the file-format plugin passes to
  `GaussianLayerWriter`, which authors it as stage custom data;
- the count and SH degree used for metadata-only authoring
  (design policy §12.3);
- the per-import statistics record (source format and version, counts, sizes,
  timings) being defined by the v0.4.0 import-statistics seam.

Anything whose presence would alter the meaning of a model array — axis hints,
unit scales, quantization tables, format version switches — must not be
retained beside the model either; it is resolved during decoding or the decode
fails. Adding a field to the model itself is a revision of this contract, not
a bundle-local decision.

## 4. Validation

`ValidateGaussianCloud` is the shared gate, and the writer calls it before
authoring anything. It enforces, in order:

1. `gaussianCount > 0`;
2. `positions`, `scales`, `rotations`, `opacities`, and `dcCoefficients` all
   have length exactly `gaussianCount`;
3. `shDegree` within the supported range and consistent with
   `CoefficientsPerGaussian()`;
4. `restCoefficients` has length `gaussianCount * (CoefficientsPerGaussian() - 1)`;
5. per Gaussian: positions, scales, rotation components, opacity, and DC
   coefficients are finite; **scales are strictly positive**; **opacity is
   within `[0, 1]`**; **the quaternion norm is within `1e-4` of 1**;
6. every rest coefficient is finite.

Two rules are easy to miss and are the ones a new decoder most often trips:
**scale must be strictly positive** (a zero scale is rejected, not clamped)
and **opacity must already be in `[0, 1]`** (the model stores no logits).

Validation is a backstop, not a substitute for format-level diagnostics. A
decoder should reject malformed input with its own specific code — a user is
better served by `GSPZ-E0xx "quaternion is not normalizable"` than by a generic
cloud-validation failure. Reaching `ValidateGaussianCloud` with invalid data
means the decoder missed a case.

Beyond the runtime gate, `CheckCloudContract`
(`openstrata/gs/testing/CloudContract.h`) pins the properties this document
states in prose, so a decoder change that silently starts emitting log-scales,
opacity logits, or unnormalized quaternions is caught in a unit test rather
than in a viewer. Every bundle runs it against its own fixtures. SH *ordering*
is the one property neither check can see — Gaussian-major and channel-major
layouts have identical lengths — which is why each bundle also verifies
ordering against hand-computed fixture values (for PLY, `TestShLayout`).

## 5. What the model deliberately does not have

- **No OpenUSD types.** `gaussianCore` must not depend on OpenUSD; that edge is
  forbidden in [WORKSPACE.md](../architecture/WORKSPACE.md).
- **No source-format identity.** The source format reaches the stage as a
  string the file-format plugin passes to the writer, not as model state.
- **No per-Gaussian source indices, no original file offsets, no quantization
  parameters.** Round-tripping back to the source encoding is out of scope
  through v1.0.0.
- **No time samples.** Animated Gaussians are post-1.0 work.
