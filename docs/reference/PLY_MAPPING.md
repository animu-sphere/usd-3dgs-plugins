# Gaussian PLY mapping contract

This is the normative mapping for the `gaussian-ply` bundle. It documents the
canonical Graphdeco-style dialect and the exact OpenUSD values the plugin
authors. Observed exporter coverage lives in [PLY_DIALECTS.md](PLY_DIALECTS.md).

## 1. Dialect signature

The file must be a PLY document with a non-empty `vertex` element. A candidate
is Gaussian-specific when the header includes at least:

```text
scale_0
rot_0
opacity
f_dc_0
```

Every required property below must then exist as a supported scalar type.

## 2. Required properties

| Source property | Meaning before conversion |
| --- | --- |
| `x`, `y`, `z` | local-space center |
| `scale_0..2` | logarithmic axis scale |
| `rot_0..3` | scalar-first quaternion `(w,x,y,z)` |
| `opacity` | opacity logit |
| `f_dc_0..2` | RGB spherical-harmonic DC coefficient |

`f_rest_*` is optional for degree 0. If present, indices must start at zero and
be contiguous. All required arrays must match the vertex count.

## 3. Semantic conversion

For Gaussian `i`:

```text
position[i] = (x, y, z)
scale[i]    = (exp(scale_0), exp(scale_1), exp(scale_2))
opacity[i]  = sigmoid(opacity)
rotation[i] = normalize((rot_0, rot_1, rot_2, rot_3))
```

The sigmoid implementation uses sign-aware branches to avoid overflow. A
zero-length quaternion is replaced with identity and reported as a warning.
Non-finite inputs or non-finite conversion results fail the load.

After the per-attribute conversions, the decoder converts the whole cloud
from the Graphdeco **right-down-front** frame into the model's **RUB**
reference frame ([ADR 0001](../adr/0001-model-frame-is-rub.md)): the shared
`FlipYZAxes` helper negates Y/Z on positions and quaternion vector parts and
applies the per-coefficient sign table to the rest SH coefficients; scales
and the DC term are unchanged. The derivation is in the ADR.

## 4. Spherical-harmonic layout

Let `R` be the number of `f_rest_*` scalar properties and
`K = R / 3 + 1`. The decoder requires:

```text
R % 3 == 0
K == (degree + 1)^2
degree <= 3
```

A higher whole degree (`K = 25`, degree 4, and beyond) is well-formed PLY but
exceeds what the shared model carries
([GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md) §3); it is rejected
as unsupported (`GSPLY-E017`), never silently truncated.

Graphdeco stores rest coefficients channel-major:

```text
all red coefficients,
then all green coefficients,
then all blue coefficients
```

The decoder reconstructs coefficient-major RGB `Float3` values. For degree 1,
nine rest scalars become:

```text
coefficient 1 = (f_rest_0, f_rest_3, f_rest_6)
coefficient 2 = (f_rest_1, f_rest_4, f_rest_7)
coefficient 3 = (f_rest_2, f_rest_5, f_rest_8)
```

The DC coefficient is coefficient zero:

```text
(f_dc_0, f_dc_1, f_dc_2)
```

## 5. OpenUSD mapping

| Semantic value | OpenUSD attribute on `/Asset/Splat` |
| --- | --- |
| positions | `positions` |
| semantic scales | `scales` |
| normalized quaternions | `orientations` |
| semantic opacities | `opacities` |
| SH degree | `radiance:sphericalHarmonicsDegree` |
| coefficient-major RGB SH | `radiance:sphericalHarmonicsCoefficients` |

`/Asset/Splat` has type `ParticleField3DGaussianSplat`. `/Asset` is an `Xform`
with `kind = component` and is the layer's default prim.

## 6. Bounds and metadata

Extent is a conservative three-sigma sphere bound around each ellipsoid. For
Gaussian `i`:

```text
radius[i] = 3 * max(scale[i].x, scale[i].y, scale[i].z)
min = component-wise minimum(position[i] - radius[i])
max = component-wise maximum(position[i] + radius[i])
```

Using the largest axis for all three extent axes remains valid regardless of
the Gaussian's orientation.

The asset prim carries `customData.gs` containing:

- `sourceFormat = "Gaussian Splatting PLY"`;
- `gaussianCount`;
- `shDegree`.

The layer uses policy defaults `upAxis = Y` and `metersPerUnit = 1` because PLY
does not standardize either value.

## 7. Ignored data

Unknown scalar vertex properties do not alter the semantic model. They are
reported in an aggregated warning so one wide PLY does not generate one message
per Gaussian or property. The common `nx`, `ny`, and `nz` placeholder properties
are ignored silently.

List-valued required properties, duplicate numeric SH indices, malformed or
truncated data, and invalid array lengths are errors.

## 8. File-format arguments

Optional import behavior is selected through standard USD file-format
arguments (`Sdf.Layer.CreateIdentifier(path, args)`); defaults import the
source unchanged.

| Argument | Range | Behavior |
| --- | --- | --- |
| `shDegree` | integer `0..3` | Cap the imported SH degree at `min(source, requested)`; higher-order rest coefficients are dropped. Never upsamples. |
| `opacityThreshold` | number `0..1` | Drop Gaussians whose decoded (sigmoid-mapped) opacity is below the threshold. Removing every Gaussian is an error, not an empty stage. |
| `scaleMultiplier` | finite number `> 0` | Multiply decoded linear scales (applied before extent computation). |

Invalid values fail the read with `GSPLY-E201`; unknown argument keys are
ignored so hosts can add their own. `customData.gs` reflects the imported
result (post-filter count, effective degree), not the source header.

## 9. Metadata-only reads

`Read(metadataOnly=true)` — for example
`Sdf.Layer.OpenAsAnonymous(path, metadataOnly=True)` — authors the `/Asset`
scaffold, stage metrics, `customData.gs`, and the SH degree attribute without
decoding vertex data. The reported count is the source header count:
`opacityThreshold` filtering requires a full read and is not applied. The
cost is header-only (~5 ms regardless of asset size; see
[performance baselines](PERFORMANCE_BASELINES.md)).

## 10. Diagnostics

Every error and warning starts with a stable bracketed identifier
(`[GSPLY-E003] ...`). The machine-readable catalog ships with the plugin at
`plugin/resources/gaussian-ply/diagnostics.json`; codes are never renumbered
or reused.
