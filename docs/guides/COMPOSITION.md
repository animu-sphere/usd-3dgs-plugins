# Composing Gaussian assets in USD

Each supported source opens as a layer with `/Asset` as its default prim.
`/Asset` is an `Xform` of kind `component`. Its child `Splat` is a
`ParticleField3DGaussianSplat` and holds the Gaussian arrays. A reference or
payload at `/Cloud` that targets the default prim places the child at
`/Cloud/Splat`.

`customData.gs` on the asset root records the source format, authored Gaussian
count, and SH degree. The source identifier belongs to the reference or
payload arc. Imported prim metadata omits the source filename and resolved
path. A native USD export retains the import summary.

## Reference a source

With the matching import plugin [activated](INSTALL.md), put this
`scene.usda` next to the `assets/` directory:

```usda
#usda 1.0
(
    defaultPrim = "Cloud"
    upAxis = "Y"
    metersPerUnit = 1
)

def Xform "Cloud" (
    prepend references = @./assets/cloud.ply@
)
{
}
```

The path is relative to the layer that authors the reference. The same
example works with a supported `cloud.spz`, `cloud.sog`, or `meta.json`.
Keep the WebP property planes beside an unbundled `meta.json`. Set `upAxis`
and `metersPerUnit` on the assembly's root layer. For an assembly with a
different axis or unit convention, author a transform on the receiving prim.

For repeated placements, author the same reference and `instanceable = true`
on each receiving prim. USD shares a prototype and exposes each `Splat` as
an instance proxy. Each instance root can have its own transform. Edits to
the Gaussian arrays belong in the shared asset layer. See
[OpenUSD scenegraph instancing](https://openusd.org/release/api/_usd__page__scenegraph_instancing.html).

## Defer loading with a payload

Replace `prepend references` with `prepend payload` in the example. Open the
stage with `LoadNone` to defer the import until the cloud is needed:

```python
from pxr import Usd

stage = Usd.Stage.Open("scene.usda", load=Usd.Stage.LoadNone)
stage.Load("/Cloud")
splat = stage.GetPrimAtPath("/Cloud/Splat")
stage.Unload("/Cloud")
```

Loading the payload imports the whole cloud. A reference is composed even
when the stage opens with `LoadNone`. See
[OpenUSD payloads](https://openusd.org/release/glossary.html#usdglossary-payload).

## Package the scene as USDZ

Convert the original capture to a native binary USD layer, then package the
assembly with its USD layer dependency. In the example, change the payload to
`@./assets/cloud.usdc@` and run:

```sh
usdcat --flatten --skipSourceFileComment --out assets/cloud.usdc assets/cloud.ply
usdzip --asset scene.usda scene.usdz
```

Conversion requires the matching importer. The recipient can open
`scene.usdz` using just an OpenUSD runtime that supports
`ParticleField3DGaussianSplat`. The archive carries the native USD layers and
preserves the payload, including `LoadNone` behavior.

The PLY and SPZ importers require filesystem access to their source files.
The SOG importer can resolve an embedded `.sog` or `meta.json` and its planes
in a manually assembled package. These source entries fall outside the USDZ
specification's permitted file types. See the
[SOG resolver matrix](../reference/RESOLVER_MATRIX.md) and the
[USDZ file type rules](https://openusd.org/release/spec_usdz.html#file-types).
