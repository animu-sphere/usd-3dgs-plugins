# Gaussian model contract

This is the normative contract for `GaussianCloudData`, the format-independent
representation every decoder targets
(`libs/gaussian-core/include/openstrata/gs/GaussianCloudData.h`). It exists so
that two independently written decoders — `gaussian-ply` today, `gaussian-spz`
in v0.3.0 — produce the same model from equivalent input, and so that
`gaussianUsd` can author either one without knowing which format it came from.

The PLY-specific half of this mapping is in [PLY_MAPPING.md](PLY_MAPPING.md);
this document owns only what is true for every format.

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

This has a consequence a second format makes concrete. Graphdeco PLY is
conventionally right-down-front (RDF, Y-down); SPZ conventionally stores
right-up-back (RUB, Y-up). Passing both through unconverted would put two
equivalent assets in different orientations on the same stage, which the
v0.3.0 equivalence criterion forbids.

Therefore:

- **The PLY-native frame is the model's reference frame.** It is the frame
  v0.1.0 and v0.2.0 already authored, and changing it would be a breaking
  change to authored USD data — a major-version change under the
  [versioning policy](../roadmap/release-plan.md#versioning-policy), not
  something a minor release may do.
- **A decoder whose format defines a different conventional frame converts
  into that reference frame**, and documents the conversion in its own mapping
  document. For SPZ this is the RUB→RDF axis flip.
- Whether the *reference frame itself* should be corrected relative to the
  declared `upAxis` is a separate open question, tracked in the
  [backlog](../roadmap/backlog.md#usd-contract-evolution). It is deliberately
  not resolved here: this document records the contract, and changing it is a
  major-version decision.

## 3. Field-by-field contract

For a cloud with `N = gaussianCount` Gaussians and SH degree `D`:

| Field | Length | Meaning and admissible values |
| --- | --- | --- |
| `positions` | `N` | Local-space centers. Finite. |
| `scales` | `N` | Per-axis standard deviations, **linear, strictly positive**. Finite. Not log-encoded. |
| `rotations` | `N` | Orientation, **scalar-first** `(real, i, j, k)`, normalized. Finite. |
| `opacities` | `N` | **In `[0, 1]`**, already through sigmoid. Finite. Not a logit. |
| `dcCoefficients` | `N` | RGB SH DC term, one triple per Gaussian. Finite. |
| `restCoefficients` | `N * ((D+1)^2 - 1)` | Non-DC RGB SH terms. Finite. |
| `shDegree` | — | `D >= 0`, and consistent with the array lengths above. |
| `gaussianCount` | — | `N > 0`. An empty cloud is an error, not an empty stage. |

`CoefficientsPerGaussian()` returns `(D+1)^2` and **includes the DC term**, so
`restCoefficients` holds one fewer triple per Gaussian than that.

### Quaternion order

`Quaternion` is scalar-first (`real, i, j, k`). This matches the Graphdeco
`rot_0..rot_3` convention directly and maps to `GfQuatf(real, i, j, k)`
unchanged. A format storing vector-first, or storing only the smallest three
components as SPZ does, reorders during decoding — never at the model boundary
and never in the writer.

Decoders normalize. A zero-length or near-zero quaternion is replaced with
identity and reported as a warning rather than failing the load.

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

## 4. Validation

`ValidateGaussianCloud` is the shared gate, and the writer calls it before
authoring anything. It enforces, in order:

1. `gaussianCount > 0`;
2. `positions`, `scales`, `rotations`, `opacities`, and `dcCoefficients` all
   have length exactly `gaussianCount`;
3. `shDegree >= 0` and consistent with `CoefficientsPerGaussian()`;
4. `restCoefficients` has length `gaussianCount * (CoefficientsPerGaussian() - 1)`;
5. per Gaussian: positions, scales, rotation components, opacity, and DC
   coefficients are finite; **scales are strictly positive**; **opacity is
   within `[0, 1]`**;
6. every rest coefficient is finite.

Two rules are easy to miss and are the ones a new decoder most often trips:
**scale must be strictly positive** (a zero scale is rejected, not clamped)
and **opacity must already be in `[0, 1]`** (the model stores no logits).

Validation is a backstop, not a substitute for format-level diagnostics. A
decoder should reject malformed input with its own specific code — a user is
better served by `GSPZ-E0xx "quaternion is not normalizable"` than by a generic
cloud-validation failure. Reaching `ValidateGaussianCloud` with invalid data
means the decoder missed a case.

## 5. What the model deliberately does not have

- **No OpenUSD types.** `gaussianCore` must not depend on OpenUSD; that edge is
  forbidden in [WORKSPACE.md](../architecture/WORKSPACE.md).
- **No source-format identity.** The source format reaches the stage as a
  string the file-format plugin passes to the writer, not as model state.
- **No per-Gaussian source indices, no original file offsets, no quantization
  parameters.** Round-tripping back to the source encoding is out of scope
  through v1.0.0.
- **No time samples.** Animated Gaussians are post-1.0 work.
