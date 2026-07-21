# Gaussian SPZ mapping contract

This is the normative mapping for the `gaussian-spz` bundle. It documents how a
decompressed SPZ v1-v3 container becomes the format-independent
[`GaussianCloudData`](GAUSSIAN_MODEL_CONTRACT.md) and the exact OpenUSD values
the plugin authors. Container-level concerns (signature detection, gzip
framing, truncation, size math) are owned by [SPZ_FORMAT.md](SPZ_FORMAT.md);
this document owns the semantic decode. It mirrors [PLY_MAPPING.md](PLY_MAPPING.md)
so the two decoders can be read side by side.

The reference for every formula below is Niantic's MIT-licensed serializer
(`src/cc/splat-utils.h` and `src/cc/load-spz.cc` at
<https://github.com/nianticlabs/spz>). Where this document states a constant,
it is quoted from that source, not inferred.

## 1. Container signature and versions

The decoder reads SPZ container versions 1-3 (gzip-wrapped, single stream).
Version 4 (ZSTD) is deferred and rejected by the container reader with the
unsupported-version diagnostic `GSPZ-E003`. See
[SPZ_FORMAT.md §3](SPZ_FORMAT.md) for the version decision.

The three supported versions differ only in two per-point encodings:

| | v1 | v2 | v3 |
| --- | --- | --- | --- |
| Positions | float16 (6 bytes) | 24-bit fixed point (9 bytes) | 24-bit fixed point (9 bytes) |
| Rotations | first-three (3 bytes) | first-three (3 bytes) | smallest-three (4 bytes) |

Every other attribute is identical across the three.

## 2. Attribute stream order

The decompressed stream is attribute-major (all of one attribute, then all of
the next) in this order, matching the reference `saveSpz`:

```text
positions, alphas, colors, scales, rotations, spherical harmonics
```

Extension records follow when the header's `0x2` flag is set; they are opaque
at the container level and ignored by the decoder with a warning
(`GSPZ-W001`).

## 3. Semantic conversion

For Gaussian `i`, with header `fractionalBits = F`:

| Attribute | Stored form | Decoded model value |
| --- | --- | --- |
| position (v1) | three float16 | `(hx, hy, hz)` |
| position (v2/v3) | three 24-bit signed fixed point | `fixed * 2^-F` |
| scale | three `uint8`, log-encoded | `exp(byte / 16 - 10)` (linear, strictly positive) |
| alpha | one `uint8` | `byte / 255` (opacity, already in `[0, 1]`) |
| color / DC | three `uint8` | `(byte / 255 - 0.5) / 0.15` |
| rotation | see §4 | normalized scalar-first quaternion |
| SH rest | `uint8` per channel per coefficient | `(byte - 128) / 128` |

The DC color scale `0.15` and the SH offset/scale `128` are the reference
constants. Alpha stores `sigmoid(logit) * 255`, so dividing by 255 recovers the
opacity directly — the model wants the post-sigmoid value, never the logit
(model contract §1). Scales are stored as log values (the same convention as
Graphdeco PLY `scale_*`), so `exp` recovers the linear scale.

Only the v1 float16 position path can introduce a non-finite value; it is
checked per Gaussian and fails with `GSPZ-E012`. Every other attribute
dequantizes from bounded integers and is finite by construction.

## 4. Rotation decoding

SPZ stores quaternions **vector-first** and omits one component, reconstructing
it from the unit-length constraint. The model is **scalar-first**
`(real, i, j, k)` (model contract §3), so the decoder reorders after decoding.

**First-three (v1, v2).** The stored bytes are `x`, `y`, `z`; the encoder forces
`w >= 0` before quantizing. Decode:

```text
c   = byte / 127.5 - 1        for each of x, y, z
w   = sqrt(max(0, 1 - x² - y² - z²))
```

**Smallest-three (v3).** A 32-bit little-endian word: bits 31-30 hold the index
of the omitted (largest, non-negative) component; below them, three 10-bit
fields of `1 sign + 9 magnitude` bits, packed most-significant-first in
increasing component index. Decode each stored component as

```text
value = sqrt(1/2) * magnitude / 511      (negated if the sign bit is set)
```

and reconstruct the omitted component as `sqrt(max(0, 1 - Σ value²))`. The
`max(0, …)` clamp guards the radicand against quantization pushing the sum of
squares slightly past 1; the reference omits it and can produce NaN for hostile
input.

Every decoded quaternion is normalized through the shared
`NormalizeQuaternion`. The decoded norm is never near zero, so the
identity-replacement warning path is unreachable for SPZ.

## 5. Coordinate system (native RUB)

SPZ conventionally stores **right-up-back** — exactly the model's reference
frame (model contract §2, [ADR 0001](../adr/0001-model-frame-is-rub.md)) —
so the decoder applies **no frame conversion**: positions, quaternions, and
SH coefficients are dequantized verbatim.

Through v0.3.0 the model frame was PLY-native RDF and this decoder negated
the Y and Z axes; that Y/Z-negation conversion — including the quaternion
vector flip and the per-coefficient `flipSh` sign table — is now the PLY
decoder's duty, implemented once as `FlipYZAxes` in `gaussianCore` and
derived in the ADR.

## 6. Spherical-harmonic layout

The SPZ SH stream is point-major with the color channel as the inner axis: per
point, per rest coefficient, three bytes `r, g, b`. That is already the model's
layout — Gaussian-major RGB triples (model contract §3) — so no transpose is
required; decoding is a dequantizing copy:

```text
model.restCoefficients[i * D + k] = dequantize(stream[(i*D + k)*3 .. +3])
```

where `D = (degree+1)² - 1` rest coefficients. The writer interleaves the DC
term (from the color stream) and the rest into the single authored
`radiance:sphericalHarmonicsCoefficients` array, DC first per Gaussian.

## 7. Supported SH degree

The container accepts the specification range (degrees 0-4). The shared model
carries degrees 0-3, so **degree 4 is rejected by the decoder with
`GSPZ-E011`** — *unsupported*, not malformed: a degree-4 SPZ file is valid, and
the user is told the degree is not carried yet rather than that the file is
corrupt. `DecodeMetadata` applies the same check so a metadata read never
promises a decode that `Read` would reject.

## 8. OpenUSD mapping

Identical to PLY ([PLY_MAPPING.md §5-§6](PLY_MAPPING.md)); SPZ authors through
the same `libs/gaussian-usd` `GaussianLayerWriter`:

| Semantic value | OpenUSD attribute on `/Asset/Splat` |
| --- | --- |
| positions | `positions` |
| linear scales | `scales` |
| normalized quaternions | `orientations` |
| opacities | `opacities` |
| SH degree | `radiance:sphericalHarmonicsDegree` |
| interleaved RGB SH | `radiance:sphericalHarmonicsCoefficients` |

`/Asset/Splat` has type `ParticleField3DGaussianSplat`. `/Asset` is an `Xform`
with `kind = component` and is the layer's default prim. Extent is the same
conservative three-sigma sphere bound as PLY. The asset prim carries
`customData.gs` with `sourceFormat = "Gaussian Splatting SPZ"`, `gaussianCount`,
and `shDegree`. The layer uses policy defaults `upAxis = Y` and
`metersPerUnit = 1`.

Because both formats author through one writer, the stage hierarchy, schema,
metadata policy, stage metrics, and default-prim behavior are identical to PLY
by construction. The Python smoke test
(`tests/test_gaussian_spz_plugin.py`) asserts this contract directly.

## 9. Ignored data

Extension records and the antialiased flag (`0x1`) are ignored, each with an
aggregated warning (`GSPZ-W001`, `GSPZ-W002`). The authored schema carries no
antialiasing convention and no extension payload, so neither affects the
decoded model.

## 10. Metadata-only reads

`Read(metadataOnly=true)` authors the `/Asset` scaffold, stage metrics,
`customData.gs`, and the SH degree attribute from the container header alone —
no attribute streams are decompressed. The reported count is the header count.

## 11. Diagnostics

Every error and warning starts with a stable bracketed identifier
(`[GSPZ-E011] ...`). The container series (`GSPZ-E001`-`E010`) is documented in
[SPZ_FORMAT.md](SPZ_FORMAT.md); the decoder adds `GSPZ-E011`-`E013`, the shared
authoring codes `GSPZ-E101`-`E104`, the read-only entry-point code `GSPZ-E201`,
and the warnings `GSPZ-W001`-`W002`. The machine-readable catalog ships at
`plugin/resources/gaussian-spz/diagnostics.json`; the smoke test cross-checks it
against the source constants in both directions. Codes are never renumbered or
reused.
