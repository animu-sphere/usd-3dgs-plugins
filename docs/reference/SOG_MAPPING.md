# Gaussian SOG mapping contract

This is the normative mapping for the `gaussian-sog` bundle. It documents how a
SOG v2 container (bundled `.sog` or unbundled `meta.json`) becomes the
format-independent [`GaussianCloudData`](GAUSSIAN_MODEL_CONTRACT.md) and the
exact OpenUSD values the plugin authors. Container-level concerns (layout
detection, ZIP walking, `meta.json` schema and image-plane validation) are
owned by [SOG_FORMAT.md](SOG_FORMAT.md); this document owns the semantic
decode. It mirrors [SPZ_MAPPING.md](SPZ_MAPPING.md) and
[PLY_MAPPING.md](PLY_MAPPING.md) so the three decoders can be read side by side.

The reference for every formula below is PlayCanvas's MIT-licensed
[SplatTransform](https://github.com/playcanvas/splat-transform) — the encoder
`src/lib/writers/write-sog.ts`, the decoder `src/lib/readers/read-sog.ts`, and
the coordinate convention `src/lib/utils/math.ts` (`Transform.PLY`) — read at
`main` while writing this document, cross-checked against the prose
[SOG format specification](https://developer.playcanvas.com/user-manual/gaussian-splatting/formats/sog/)
and the [v2 proposal](https://github.com/playcanvas/splat-transform/issues/38).
Where a constant is stated here it is quoted from that source, not inferred; the
one place the prose and the implementation disagree (the coordinate frame) is
recorded in §5 and resolved in favour of the implementation.

## 1. Container, version, and layouts

The decoder reads **SOG v2** — the `meta.json` `version: 2` generation — in both
layouts, converging on one semantic decode:

- **bundled** — one `.sog` file, a ZIP archive holding `meta.json` plus the
  lossless-WebP property images;
- **unbundled** — `meta.json` opened directly, the property images loaded as
  resolver-backed companion files from its directory.

Legacy SOG v1 (no `version` field, per-channel `mins`/`maxs` instead of
codebooks) and streamed SOG (`lod-meta.json`, spatial chunks, LOD) are **not**
read: v1 is rejected with an unsupported-version diagnostic, and streamed SOG is
deferred to SOG milestones M2-M4. See [SOG_FORMAT.md](SOG_FORMAT.md) for the
version and layout decisions and the container diagnostics.

## 2. meta.json schema and property planes

`meta.json` names the count, the per-property affine ranges and codebooks, and
the WebP files. All images are **8-bit RGBA lossless WebP**, row-major with the
texel for Gaussian `i` at `x = i % W`, `y = i / W` (`i = x + y*W`); a lossy
plane would corrupt positions and is rejected as malformed.

| Property | meta fields | WebP file(s) | Channels used |
| --- | --- | --- | --- |
| `means` | `mins[3]`, `maxs[3]` (log-domain) | `means_l.webp`, `means_u.webp` | RGB = low / high byte of a 16-bit code per axis |
| `scales` | `codebook[256]` (log-domain) | `scales.webp` | RGB = codebook index per axis |
| `quats` | — | `quats.webp` | RGB = packed components, A = largest-component tag |
| `sh0` | `codebook[256]` (raw DC) | `sh0.webp` | RGB = codebook index per channel, A = opacity |
| `shN` (optional) | `count`, `bands` (1-3), `codebook[256]` | `shN_centroids.webp`, `shN_labels.webp` | see §6 |

`meta.count <= W*H` for every plane (`meta.count` is the Gaussian count, distinct
from `meta.shN.count` above). `meta.count == 0` is a valid empty cloud. The plane
dimensions and presence, and the codebook lengths, are validated by the reader
([SOG_FORMAT.md](SOG_FORMAT.md)) before the decoder runs.

## 3. Semantic conversion

For Gaussian `i`, decoding each plane's texel into the model value:

| Attribute | Stored form | Decoded model value |
| --- | --- | --- |
| position | two RGB texels (low, high byte), per-axis log-range | `invLog(lerp(mins[a], maxs[a], code/65535))`, see §4 |
| scale | three `uint8` codebook indices | `exp(scales.codebook[byte])` (linear, strictly positive) |
| rotation | RGB packed + A tag | normalized scalar-first quaternion, see §5 |
| opacity | one `uint8` (`sh0` alpha) | `byte / 255` (already in `[0, 1]`) |
| DC / color | three `uint8` codebook indices | `sh0.codebook[byte]` (raw band-0 SH coefficient) |
| SH rest | palette label → centroid indices → codebook | `shN.codebook[byte]`, see §6 |

The transforms mirror Graphdeco PLY: SOG stores the same physical quantities a
Graphdeco `.ply` stores (log scales, opacity as a post-sigmoid byte via the
codebook's DC values, raw `f_dc`/`f_rest` SH coefficients), only codebook- and
image-quantized. The encoder writes `opacity` as `sigmoid(logit) * 255`
(`write-sog.ts`), so dividing the `sh0` alpha by 255 recovers the model's
post-sigmoid opacity directly — the model never sees a logit
([model contract](GAUSSIAN_MODEL_CONTRACT.md) §1). The `scales` and `sh0`
codebooks store **log scales** and **raw DC coefficients** respectively, not
render-ready values: the specification's display formulae
`sx = exp(codebook[i])` and `color = 0.5 + codebook[i] * SH_C0`
(`SH_C0 = 0.28209479177387814`) confirm that `exp` is applied to scales while
the DC coefficient is carried into the model raw, exactly as PLY carries
`f_dc_*` (the writer's `sh0` codebook is k-means over `[f_dc_0, f_dc_1,
f_dc_2]`).

Every plane dequantizes from bounded integers, so the only non-finite risk is a
malformed `meta.json` numeric field (`mins`/`maxs`/`codebook`); those are
range/finite-checked by the reader before decode.

## 4. Positions (split-precision + inverse-log)

Positions are stored 16-bit per axis, split low byte in `means_l.webp` and high
byte in `means_u.webp`, as a normalized code into a per-axis **log-domain**
range. The encoder applies `logTransform(v) = sign(v) * ln(|v| + 1)` before
quantizing and stores the log-domain axis bounds in `meta.means.mins`/`maxs`
(`write-sog.ts`). Decode (`read-sog.ts`):

```text
code = means_l[axis] | (means_u[axis] << 8)          # 0..65535
n    = mins[axis] + (maxs[axis] - mins[axis]) * code / 65535
p    = sign(n) * (exp(|n|) - 1)                       # invLogTransform
```

The log transform gives near-origin content more code resolution than a linear
16-bit range would; `invLogTransform` is its exact inverse. A zero-width axis
range (`maxs == mins`) decodes every code to the same value and is not an error;
the decoder guards the division as the reference does (`scale || 1`).

## 5. Rotation and coordinate system

### Smallest-three quaternion

SOG stores quaternions **smallest-three**: the three non-largest components in
RGB and a 2-bit tag in the alpha byte naming which component was dropped. The
encoder normalizes, forces the largest component non-negative, scales by
`sqrt(2)`, and writes `byte = 255 * (component * 0.5 + 0.5)` with
`alpha = 252 + largestIndex` (`write-sog.ts`). Decode (`read-sog.ts`
`unpackQuat`), producing the model's **scalar-first** `(w, x, y, z)`:

```text
maxComp = alpha - 252                     # 0=w, 1=x, 2=y, 3=z; else malformed
c       = (byte / 255 * 2 - 1) / sqrt(2)  # for each of the three stored bytes
```

The three decoded values fill the non-`maxComp` slots (slot order
`QUAT_IDX[maxComp]`); the dropped component is reconstructed non-negative as
`sqrt(max(0, 1 - Σc²))`. The `max(0, …)` clamp guards the radicand against
quantization pushing the sum of squares past 1. An alpha byte outside
`{252, 253, 254, 255}` is a malformed rotation and fails the decode with a
`GSSOG-****` code rather than being silently replaced with identity (the model
requires a valid normalized quaternion; a corrupt tag is not a valid one).

The four logical slots are the Graphdeco `rot_0..rot_3` in scalar-first order,
identical to the model's convention
([model contract](GAUSSIAN_MODEL_CONTRACT.md) §3), so the reordering is the
smallest-three unpack only; every decoded quaternion then passes through the
shared `NormalizeQuaternion`.

### Coordinate frame (PLY-native RDF)

**SOG stores Graphdeco-PLY-convention data, so the decoder applies the same
RDF→RUB conversion the PLY decoder applies — the shared `FlipYZAxes` helper —
never the writer, never the consumer.** This is the frame duty
[ADR 0001](../adr/0001-model-frame-is-rub.md) anticipated for SOG and deferred
to "its mapping document when the decoder lands"; it is confirmed here.

The evidence is the reference implementation, not the prose. SplatTransform's
writer bakes every source to `Transform.PLY` before encoding
(`bakeTransform(source, Transform.PLY)`), and its reader reports the decoded
source's frame as `Transform.PLY` with no per-component negation in the decode
loop. `Transform.PLY` is documented in `math.ts` as the coordinate convention
shared by "PLY, splat, KSplat, SPZ, and SOG": a SOG file therefore stores the
same raw column values a Graphdeco `.ply` stores. Because this project defines
the Graphdeco convention as **RDF (Y-down)** and converts it to the model's
**RUB (Y-up)** frame with `FlipYZAxes`
([ADR 0001](../adr/0001-model-frame-is-rub.md)), SOG reuses that conversion
verbatim: Y and Z negate on positions, on the quaternion vector part, and on
the non-DC SH coefficients via the shared `kShFlipYZ` sign table.

The prose specification instead describes a right-handed **y-up, z-back** frame.
That is the PlayCanvas *engine* frame the runtime presents after applying
`Transform.PLY` (a 180° rotation to engine space), not the on-disk column
frame; taking it literally would place equivalent PLY and SOG assets in
different orientations on the same stage, which the cross-format equivalence
criterion forbids. The discrepancy is resolved in favour of the implementation,
mirroring the prose-vs-serializer corrections recorded in
[SPZ_FORMAT.md §4](SPZ_FORMAT.md). The PLY/SPZ/SOG equivalence triples in
`tests/equivalence/` are the end-to-end check: PLY and SOG apply every sign,
SPZ applies none, and all three must agree.

## 6. Spherical-harmonic layout

**DC (band 0).** `sh0.webp` RGB are three codebook indices; the model DC
coefficients are `sh0.codebook[r]`, `sh0.codebook[g]`, `sh0.codebook[b]` — the
raw `f_dc_*` values, no color transform (the `0.5 + c*SH_C0` mapping is a render
step, not a model value). Opacity is the `sh0` alpha (§3).

**Rest (bands 1-3).** SOG palette-compresses the higher-order SH: `meta.shN`
carries a per-Gaussian palette of `count` centroids (`shN_labels.webp`) plus a
shared 256-entry `codebook`, and the centroid values live in
`shN_centroids.webp`. `meta.shN.bands ∈ {1, 2, 3}` gives
`shCoeffs = {3, 8, 15}` rest coefficients per color channel (the Graphdeco
`(D+1)² − 1` counts for degrees 1-3). Decode (`read-sog.ts`):

```text
label = shN_labels.r | (shN_labels.g << 8)      # 16-bit palette index
                                                # label >= count -> rest = 0
cy    = label / 64                              # centroid texel row
cx    = (label % 64) * shCoeffs + coeff         # centroid texel column
```

The centroids texel's R/G/B are codebook indices for coefficient `coeff` of the
red / green / blue channel: `shCodebook[R]`, `shCodebook[G]`, `shCodebook[B]`.
The reference emits these **channel-major** (all red coefficients, then green,
then blue) — the Graphdeco `f_rest` order. The model wants Gaussian-major RGB
triples ([model contract](GAUSSIAN_MODEL_CONTRACT.md) §3), so the decoder
transposes exactly as the PLY decoder transposes Graphdeco `f_rest`, then
applies the RDF→RUB SH sign table (§5). The centroids plane width is required to
equal `64 * shCoeffs`; a label past `count` yields zero rest coefficients for
that Gaussian (the reference's out-of-range guard).

## 7. Supported SH degree

`meta.shN.bands` is 1-3, i.e. SH degree 1-3; a file with no `shN` is degree 0.
The whole SOG v2 range therefore lands inside the model's supported degrees 0-3,
so — unlike SPZ, whose header admits a degree 4 the model does not carry — SOG
needs no degree-ceiling rejection. A `bands` value outside `{1, 2, 3}` (with
`shN` present) is a malformed `meta.json` and is rejected by the container
reader, not silently degraded.

## 8. OpenUSD mapping

Identical to PLY and SPZ; SOG authors through the same `libs/gaussian-usd`
`GaussianLayerWriter`:

| Semantic value | OpenUSD attribute on `/Asset/Splat` |
| --- | --- |
| positions | `positions` |
| linear scales | `scales` |
| normalized quaternions | `orientations` |
| opacities | `opacities` |
| SH degree | `radiance:sphericalHarmonicsDegree` |
| interleaved RGB SH | `radiance:sphericalHarmonicsCoefficients` |

`/Asset/Splat` has type `ParticleField3DGaussianSplat`; `/Asset` is an `Xform`
with `kind = component` and is the layer's default prim. Extent is the same
conservative three-sigma sphere bound as PLY/SPZ, through the shared
`ComputeCloudExtent`. The asset prim carries `customData.gs` with
`sourceFormat = "Gaussian Splatting SOG"`, `gaussianCount`, and `shDegree`. The
layer uses policy defaults `upAxis = Y` and `metersPerUnit = 1`. Because all
three formats author through one writer, the stage hierarchy, schema, metadata
policy, stage metrics, and default-prim behavior are identical to PLY and SPZ by
construction; the Python smoke test asserts this contract directly.

## 9. Metadata-only reads

`Read(metadataOnly=true)` authors the `/Asset` scaffold, stage metrics,
`customData.gs`, and the SH degree from `meta.json` alone — no WebP texture is
decoded (the reference's `statSogSource` reads exactly this much). The reported
count is `meta.count`; the SH degree is `meta.shN.bands` (0 when absent).

## 10. Diagnostics

Every error and warning starts with a stable bracketed identifier
(`[GSSOG-E0xx] ...`) joined in `gaussianCore` so the spelling cannot drift
between bundles. The container series and the full allocation are documented in
[SOG_FORMAT.md](SOG_FORMAT.md) and shipped in the machine-readable catalog
`plugin/resources/gaussian-sog/diagnostics.json`, which the smoke test
cross-checks against the source constants in both directions. The skeleton's
`GSSOG-E001` (SOG not implemented) is retired once decoding lands and is never
reused. Codes are never renumbered or reused.
