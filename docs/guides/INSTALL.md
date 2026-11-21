# Building and installing USD 3DGS Plugins

The [latest stable release](https://github.com/animu-sphere/usd-3dgs-plugins/releases/latest)
provides product archives containing the `gaussian-ply`, `gaussian-spz`,
`gaussian-sog`, and (from v0.12.0) `gaussian-splat` member bundles. See the
[release records](../releases/README.md) for the verification results for each
release.
Check [SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md)
before reusing a binary package: OpenUSD plugin binaries must match the target
platform, compiler ABI, OpenUSD build, and Python ABI.

## Install a binary release

With a matching OpenUSD environment active, download the target product
archive and [verify it](#verify-a-downloaded-release-artifact). OpenStrata
installs the members into a new directory below the chosen prefix:

```sh
ost plugin product install ./name-of-the-downloaded-product.tar.zst --prefix ./usd-3dgs
. ./usd-3dgs/activate.sh
usdcat --flatten --out scene.usda scene.ply
```

In PowerShell, dot-source `. .\usd-3dgs\activate.ps1` instead of
`activate.sh`. For a Python host, run the installed
`openstrata_activate.py` in the host process before importing `pxr`.
If activating an extracted standalone bundle, use
[manual package activation](#manual-package-activation).

## With OpenStrata

From the repository root:

```sh
ost runtime pull cy2026 --profile usd
ost plugin build plugins/gaussian-ply
ost plugin doctor plugins/gaussian-ply
ost plugin test --workspace --up-to 5
```

The workspace ships independent bundles for PLY, SPZ, SOG, and `.splat`. See the
[capability matrix](../reference/CAPABILITY_MATRIX.md) for supported inputs.
The bundles install and activate the same way — substitute
`plugins/gaussian-spz`, `plugins/gaussian-sog`, or `plugins/gaussian-splat` in the per-bundle commands
throughout this guide. Install only the ones you need; none depends on another.

One caveat specific to `gaussian-sog`: because the stock unbundled layout is
named `meta.json`, that bundle registers the `.json` extension as well as
`.sog`. It claims a `.json` file only when the file actually parses as a SOG v2 or v3
metadata document, so unrelated JSON is left alone — but if your environment
already has a plugin claiming `.json`, install `gaussian-sog` deliberately
rather than by default.

Run a USD tool with the plugin environment composed automatically:

```sh
ost plugin run plugins/gaussian-ply -- usdcat --flatten \
  plugins/gaussian-ply/tests/fixtures/one-gaussian-ascii.ply
```

The flattened USDA shows the `Asset` default prim and its `Splat` child of
type `ParticleField3DGaussianSplat`. A SuperSplat compressed `.ply` opens the
same way through `gaussian-ply`, which routes it by its header; its stage
records `sourceFormat = "Gaussian Splatting compressed PLY"`. This is a
headless import check. For a viewport check of a Gaussian PLY, see the separate
[Merlin preview](../reference/CAPABILITY_MATRIX.md#hydra-rendering).

For interactive inspection:

```powershell
ost plugin view plugins\gaussian-ply "C:\path\to\scene.ply"
```

Flatten the plugin-backed stage to a binary `.usd` file with no source-path
comment:

```powershell
ost plugin run plugins\gaussian-ply -- usdcat --flatten --skipSourceFileComment --usdFormat usdc --out "C:\path\to\scene.usd" "C:\path\to\scene.ply"
```

## SOG usage examples

The bundled layout is one `.sog` file. Open it through the SOG bundle or
flatten it to a standalone binary USD layer:

```powershell
ost plugin view plugins\gaussian-sog "plugins\gaussian-sog\tests\fixtures\kit-one-degree0.sog"
ost plugin run plugins\gaussian-sog -- usdcat --flatten --skipSourceFileComment --usdFormat usdc --out scene-sog.usdc "plugins\gaussian-sog\tests\fixtures\kit-one-degree0.sog"
```

The unbundled layout starts at `meta.json`; its lossless-WebP companion planes
are resolved from the same directory:

```powershell
ost plugin view plugins\gaussian-sog "plugins\gaussian-sog\tests\fixtures\unbundled-kit-multi-degree3\meta.json"
ost plugin run plugins\gaussian-sog -- usdcat --flatten --skipSourceFileComment --usdFormat usda --out scene-sog-unbundled.usda "plugins\gaussian-sog\tests\fixtures\unbundled-kit-multi-degree3\meta.json"
```

Both source layouts author the same stage contract:

```text
/Asset                  Xform, kind=component, defaultPrim
  /Splat              ParticleField3DGaussianSplat
```

The `/Asset` prim carries `customData.gs` so downstream tools can identify the
source without reopening the SOG container. A degree-1 fixture produces values
of this shape:

```usda
customData = {
  dictionary gs = {
    uint64 gaussianCount = 2
    int shDegree = 1
    string sourceFormat = "Gaussian Splatting SOG"
  }
}
```

The source SOG or `meta.json` is read as a plugin-backed USD layer. `usdcat`
flattens that layer into `.usda` or `.usdc` while retaining `/Asset/Splat`; a
viewer or downstream Hydra delegate then consumes the authored stage. Stock
`usdview` can inspect the stage but does not render the splats unless its active
delegate implements `ParticleField3DGaussianSplat`.

The plugin only authors Gaussian schema data; this repository does not provide
a Hydra renderer. Opening a stage and rendering visible splats are separate
capabilities.

Examples viewed with OpenUSD's `hdParticleField` delegate:
[PLY](../assets/usdview_hdParticleField_PLY.png),
[SOG](../assets/usdview_hdParticleField_SOG.png), and
[SPZ](../assets/usdview_hdParticleField_SPZ.png).

## `.splat` usage example

A headerless `.splat` file opens through the `gaussian-splat` bundle:

```powershell
ost plugin run plugins\gaussian-splat -- usdcat --flatten --skipSourceFileComment --usdFormat usda --out scene-splat.usda "plugins\gaussian-splat\tests\fixtures\decode-known.splat"
```

The stage records `sourceFormat = "Gaussian Splatting SPLAT"` and SH degree 0,
because the format stores only the base colour. `.splat` has no header, so
the bundle claims a file by its extension and a size that is a multiple of
32 bytes; see [SPLAT_FORMAT.md](../reference/SPLAT_FORMAT.md#4-routing) for
the routing rule and the quantization loss to expect.

## Package and verify

```sh
ost plugin package --workspace --product
ost plugin test --workspace --from-package --up-to 5
```

The package-origin command extracts the generated artifacts to a clean staging
directory before running discovery, `usdcat`, and Python stage-open checks for
all four bundles. The package is written under:

```text
plugins/<bundle>/dist/plugins/<bundle>/<version>/<target>/
```

alongside `manifest.json`, `sbom.spdx.json`, and `SHA256SUMS`.

Use the verification level for your environment in
[supported configurations](../reference/SUPPORTED_CONFIGURATIONS.md).
[Release records](../releases/) document the actual package results.

### Verify the aggregate installation

From the repository root after building all four bundles:

```sh
ost plugin package --workspace --product --json > build/product-package.json
ost plugin run plugins/gaussian-ply --no-inject -- python tests/package_consumer/test_product.py run build/product-package.json
```

The test installs the aggregate archive with `ost plugin product install` in
a temporary directory. Fresh Python and C++ consumers open the packaged
Graphdeco PLY, compressed PLY, SPZ version 2 and 4, bundled SOG, and `.splat` fixtures. The Python
consumer checks that the loaded libraries and diagnostic catalogs come from the
active prefix and that each stage records its source format. `usdcat`
flattens each stage and `usdchecker` validates the resulting USD.

OST installs to a new prefix. To switch prefixes, activate the new one in a
fresh process. To uninstall an isolated installation, close the processes
using it, remove its directory, and remove its search paths from the next
session. The test checks prefix precedence with the first install still
present, then checks that stale search paths discover none of the four plugins
after both test prefixes are removed. The test is part of the release workflow
and passed locally on Windows with OpenUSD 26.08 and OpenStrata 0.23.20.

## Manual package activation

After extracting the target-matching archive:

- add `plugin/resources/gaussian-ply` to `PXR_PLUGINPATH_NAME`;
- make the package `lib` directory available to the platform dynamic loader;
- retain the OpenUSD installation's own plugin and library paths.

On Windows, use `;` as the path separator; Linux and macOS use `:`. Python hosts
on Windows may need `os.add_dll_directory()` for the extracted `lib` directory.

This path is verified on Windows: with `PXR_PLUGINPATH_NAME` pointing at the
extracted `plugin/resources/gaussian-ply` and the runtime's `bin` and `lib`
directories on `PATH`, both `usdcat` and a plain Python host opened the
packaged fixtures with no `ost` involvement. Two observations from that run:

- `usdcat` embeds Python, so the matching Python runtime DLL
  (`python313.dll` for this target) must also be resolvable, or `usdcat`
  exits immediately with no output.
- The generated `plugInfo.json` records an absolute `LibraryPath` after
  extraction is scanned, so the observed host did not additionally need the
  extracted `lib` directory on the loader path, and the Python host needed no
  `os.add_dll_directory()` call. Keep both in place when scripting the generic
  procedure; they are the documented contract for hosts that resolve
  differently.

`ost plugin run <extracted-package>` remains the recommended path because OST
composes the environment from the package manifest. Outside a workspace, pass
the target and profile explicitly, for example
`ost plugin run <extracted-root> --target cy2026 --profile usd -- usdcat <fixture>`.

## Verify a downloaded release artifact

Download the target archive and `SHA256SUMS` from the same GitHub release
into one directory. Choose the entry for the exact archive filename. On
macOS, use `shasum -a 256 -c`; on Linux, use `sha256sum -c` as below:

```sh
archive='name-of-the-downloaded-product.tar.zst'
entry=$(awk -v archive="$archive" '$2 == archive { print }' SHA256SUMS)
test -n "$entry" && printf '%s\n' "$entry" | sha256sum -c
```

On Windows PowerShell, use the equivalent check:

```powershell
$archive = 'name-of-the-downloaded-product.tar.zst'
$entry = Get-Content .\SHA256SUMS | Where-Object { ($_ -split '\s+', 2)[1] -ceq $archive }
if (-not $entry) { throw "No checksum published for $archive" }
$expected = ($entry -split '\s+', 2)[0]
$actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash
if ($actual -ne $expected) {
  throw "$archive has SHA-256 $actual, expected $expected"
}
```

The published aggregate includes its manifest, member SBOMs, and provenance.
Before activation, verify every member and confirm the displayed target,
version, and bundle names match the release:

```sh
ost plugin product verify ./name-of-the-downloaded-product.tar.zst
```

The checksum list verifies integrity and is not a signature. Resolve any
checksum or target mismatch before installing.

## Plain CMake build

Point `CMAKE_PREFIX_PATH` at an OpenUSD 26.08 installation that provides a
`pxr` CMake package:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/openusd
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The root build composes `gaussianCore` and every discovered plugin bundle. For
a bundle-only build, install `gaussianCore` first and make its install prefix
available through `CMAKE_PREFIX_PATH`.

The root path has been exercised on Windows with the Visual Studio 2022
generator: it discovered `plugins/gaussian-ply`, built both components, and
passed all three CTest entries.

## Troubleshooting

- **`.ply` is not recognized**: `PXR_PLUGINPATH_NAME` does not point to the
  directory containing the generated `plugInfo.json`, or the plugin library
  failed to load.
- **The library fails to load**: the package target does not match the host
  OpenUSD/ABI, or the extracted `lib` directory is not on the loader path.
- **`usdcat` exits immediately with no output**: the OpenUSD tools embed
  Python; the matching Python DLL (`python313.dll` for cy2026 targets) is not
  resolvable by the dynamic loader.
- **A normal mesh PLY is rejected**: expected. The bundle recognizes only the
  documented Gaussian dialect.
- **The stage opens but nothing is drawn**: the USD data contract is available,
  but the active Hydra renderer may not implement Gaussian splat rendering.
- **A large file consumes substantial memory**: opening a Gaussian cloud
  materializes its arrays. See [payload loading](COMPOSITION.md) for how to
  defer opening the asset.
