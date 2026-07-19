# PLY dialect compatibility

Observed compatibility between real exporter output and the
[Gaussian PLY mapping contract](PLY_MAPPING.md). Property resolution is
name-based and `f_rest` indices are parsed numerically, so declaration order
never matters; the three exporters below ship three different orders and all
decode identically. Import support is a file-format claim only — rendering is
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

## Property aliases

None. Every observed exporter uses the canonical Graphdeco property names,
so the decoder accepts no aliases; an alias is added only when a real
exporter's output requires it (tracked in the
[backlog](../roadmap/backlog.md#ply-compatibility)).

## Known unsupported cases

Unsupported input fails with a stable diagnostic
([diagnostics catalog](../../plugins/gaussian-ply/plugin/resources/gaussian-ply/diagnostics.json));
it is never guessed at.

| Input | Behavior |
| --- | --- |
| SuperSplat **compressed** `.ply` (chunked `packed_*` properties) | Rejected as an unsupported dialect (`GSPLY-E001`); the packed layout is a different format, not a property reordering |
| `binary_big_endian` PLY | Unsupported; not claimed until a deterministic fixture passes on every supported platform |
| Mesh or plain point-cloud PLY without the Gaussian signature | Rejected (`GSPLY-E001`); `CanRead` also returns false so other `.ply` plugins can claim the file |
| List-valued required or SH properties | Rejected (`GSPLY-E004`) |
| Non-finite values, or `double` values outside `float` range | Rejected (`GSPLY-E010` / `GSPLY-E011`); in-range doubles narrow losslessly enough for import |
| Truncated or inconsistent payloads | Rejected (`GSPLY-E014` / `GSPLY-E015`) |
| `.splat`, `.ksplat`, `.spz`, SOG | Different container formats, out of PLY scope; SPZ is the v0.3.0 theme in the [release plan](../roadmap/release-plan.md) |

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
