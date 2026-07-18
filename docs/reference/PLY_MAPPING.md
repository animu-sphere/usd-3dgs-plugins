# Gaussian PLY mapping contract

This is the normative mapping for the `gaussian-ply` bundle. It documents the
canonical Graphdeco-style dialect accepted by v0.1 and the exact OpenUSD values
it authors.

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

## 4. Spherical-harmonic layout

Let `R` be the number of `f_rest_*` scalar properties and
`K = R / 3 + 1`. The decoder requires:

```text
R % 3 == 0
K == (degree + 1)^2
```

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
