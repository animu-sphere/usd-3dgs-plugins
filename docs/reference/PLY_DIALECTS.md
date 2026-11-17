# PLY dialect compatibility

Observed compatibility between real exporter output and the two dialects the
`gaussian-ply` bundle reads: the Graphdeco dialect
([mapping contract](PLY_MAPPING.md)) and SuperSplat compressed PLY
([mapping contract](COMPRESSED_PLY_MAPPING.md)). Property resolution is
name-based in both dialects, and Graphdeco `f_rest` indices are parsed
numerically, so declaration order never matters; the three Graphdeco
exporters below ship three different orders and all decode identically. Import support is a file-format claim only — rendering is
a separate concern ([capability matrix](CAPABILITY_MATRIX.md)).

## Observed compatible dialects

| Exporter | Property layout observed | Verified with | Result |
| --- | --- | --- | --- |
| Graphdeco reference (INRIA `gaussian-splatting`) | `x y z nx ny nz f_dc f_rest_0..44 opacity scale rot`; carries placeholder normals | Mip-NeRF 360 `garden` pretrained capture, 5,834,784 Gaussians, SH degree 3 (2026-07-19) | ✅ full import; normals ignored silently |
| Brush v0.3.0 | `f_dc f_rest opacity scale rot x y z`-family with `f_rest_*` declared in **lexicographic** order (`f_rest_0, f_rest_1, f_rest_10, ...`); no normals | committed corpus (`yashica-t4`, `leica-sofort`, 8,192 each) and local 59,559-Gaussian capture, SH degree 3 | ✅ full import; numeric index parsing restores the layout |
| Jawset Postshot | `x y z f_dc f_rest opacity scale rot`; no normals | local `cactus` series, 139,410 / 464k / 719k / 1,935,120 Gaussians, SH degree 3 | ✅ full import |
| SuperSplat (uncompressed `.ply` export) | Same property set as the Graphdeco reference | Not yet observed with a real export; the scrambled-order fixture `reordered-properties.ply` covers the layout mechanics | ☑ expected compatible; flag an issue with a sample if it fails |

Supported SH degrees are 0-3 (1, 4, 9, or 16 coefficients per channel);
degrees 0-3 are fixture- and asset-verified. ASCII and binary little-endian
encodings are both supported and produce identical stages.

## Observed compressed producers

Every producer below writes binary little-endian with the 18-property
`chunk`, which is the supported layout
([COMPRESSED_PLY_MAPPING.md §2](COMPRESSED_PLY_MAPPING.md)).

| Producer | Verified with | Result |
| --- | --- | --- |
| SplatTransform 3.11.2 | committed CC0 conversions of the corpus (`yashica-t4`, `leica-sofort`, 8,192 each, SH degree 3), matched row by row to their source PLYs within the quantization bounds | ✅ full import |
| SuperSplat 2.4.1 | local exports of the Postshot `cactus` series, 139,410 to 1,935,120 Gaussians, SH degree 3 (2026-11-17). The 139,410 export imports with the same position percentiles, mean opacity, mean DC, and band-1 SH sign as its uncompressed Postshot source. | ✅ full import; not redistributable |
| SuperSplat 2.6.1, 2.7.1 | PlayCanvas engine example assets `skull` (246,821, SH degree 3) and `guitar` (90,854, degree 0) (2026-11-17) | ✅ full import; no licence file, so not redistributable |

## Property aliases

None. Every observed exporter uses the canonical Graphdeco property names,
so the decoder accepts no aliases. An alias requires a real exporter sample
and a deterministic fixture.

## Known unsupported cases

Unsupported input fails with a stable diagnostic
([diagnostics catalog](../../plugins/gaussian-ply/plugin/resources/gaussian-ply/diagnostics.json));
it is never guessed at.

| Input | Behavior |
| --- | --- |
| Compressed PLY as ASCII or big-endian, or with the 12-property `chunk` | Rejected (`GSPLY-E020` / `GSPLY-E021`); see [COMPRESSED_PLY_MAPPING.md §2](COMPRESSED_PLY_MAPPING.md) for every compressed rejection |
| `packed_*` vertex properties without a `chunk` element | Rejected as an unsupported dialect (`GSPLY-E001`) |
| `binary_big_endian` PLY | Unsupported; not claimed until a deterministic fixture passes on every supported platform |
| Mesh or plain point-cloud PLY without the Gaussian signature | Rejected (`GSPLY-E001`); `CanRead` also returns false so other `.ply` plugins can claim the file |
| List-valued required or SH properties | Rejected (`GSPLY-E004`) |
| Non-finite values, or `double` values outside `float` range | Rejected (`GSPLY-E010` / `GSPLY-E011`); in-range doubles narrow losslessly enough for import |
| Truncated or inconsistent payloads | Rejected (`GSPLY-E014` / `GSPLY-E015`) |
| `.splat`, `.ksplat`, `.spz`, SOG | Separate formats; see the [capability matrix](CAPABILITY_MATRIX.md) for import support |

## Re-checking a new exporter

Run a metadata-only probe first (header cost only, ~5 ms at any size), then a
full open:

```python
from pxr import Sdf, Usd
layer = Sdf.Layer.OpenAsAnonymous("asset.ply", metadataOnly=True)
print(layer.GetPrimAtPath("/Asset").customData["gs"])
stage = Usd.Stage.Open("asset.ply")
```

If the header layout is rejected, the error names the exact property and
carries a `GSPLY-E***` code from the catalog.
