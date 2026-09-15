# Building and installing USD 3DGS Plugins

The latest tagged and published release is v0.5.0
([release records](../releases/README.md)). The verified paths are an
OpenStrata source build, a
locally generated OpenStrata package, and manual activation of an extracted
package on Windows. The v0.5.0 product archive contains the `gaussian-ply`,
`gaussian-spz`, and `gaussian-sog` member bundles.
Check [SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md)
before reusing a binary package: OpenUSD plugin binaries must match the target
platform, compiler ABI, OpenUSD build, and Python ABI.

## With OpenStrata

From the repository root:

```sh
ost runtime pull cy2026 --profile usd
ost plugin build plugins/gaussian-ply
ost plugin doctor plugins/gaussian-ply
ost plugin test --workspace --up-to 5
```

The workspace ships three independent plugin bundles: `gaussian-ply` for
Graphdeco-style `.ply`, `gaussian-spz` for Niantic `.spz` (container versions
1-3), and `gaussian-sog` for PlayCanvas SOG v2 (bundled `.sog` and unbundled
`meta.json`). They install and activate the same way — substitute
`plugins/gaussian-spz` or `plugins/gaussian-sog` in the per-bundle commands
throughout this guide. Install only the ones you need; none depends on another.

One caveat specific to `gaussian-sog`: because the stock unbundled layout is
named `meta.json`, that bundle registers the `.json` extension as well as
`.sog`. It claims a `.json` file only when the file actually parses as a SOG v2
metadata document, so unrelated JSON is left alone — but if your environment
already has a plugin claiming `.json`, install `gaussian-sog` deliberately
rather than by default.

Run a USD tool with the plugin environment composed automatically:

```sh
ost plugin run plugins/gaussian-ply -- usdcat --flatten \
  plugins/gaussian-ply/tests/fixtures/one-gaussian-ascii.ply
```

For interactive inspection:

```powershell
ost plugin view plugins\gaussian-ply "C:\path\to\scene.ply"
```

The local cactus dogfooding sample was opened with:

```powershell
ost plugin view plugins\gaussian-ply "C:\Users\snkm\Desktop\testdata_3dgs\3DGS_PLY_sample_data\PLY(postshot)\cactus_splat3_30kSteps_142k_splats.ply"
```

Flatten the plugin-backed stage to a binary `.usd` file with no source-path
comment:

```powershell
ost plugin run plugins\gaussian-ply -- usdcat --flatten --skipSourceFileComment --usdFormat usdc --out "C:\path\to\scene.usd" "C:\path\to\scene.ply"
```

The cactus sample was flattened next to its source with:

```powershell
ost plugin run plugins\gaussian-ply -- usdcat --flatten --skipSourceFileComment --usdFormat usdc --out "C:\Users\snkm\Desktop\testdata_3dgs\3DGS_PLY_sample_data\PLY(postshot)\cactus_splat3_30kSteps_142k_splats.usd" "C:\Users\snkm\Desktop\testdata_3dgs\3DGS_PLY_sample_data\PLY(postshot)\cactus_splat3_30kSteps_142k_splats.ply"
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

## Package and verify

```sh
ost plugin package --workspace --product
ost plugin test --workspace --from-package --up-to 5
```

The package-origin command extracts the generated artifacts to a clean staging
directory before running discovery, `usdcat`, and Python stage-open checks for
all three bundles. The package is written under:

```text
plugins/<bundle>/dist/plugins/<bundle>/<version>/<target>/
```

alongside `manifest.json`, `sbom.spdx.json`, and `SHA256SUMS`.

The current package-origin run passes discovery, read, and stage-open checks
for all three bundles on the Windows 0.22.10 baseline. The source-workspace L5
checks also pass locally. Hosted release dry-run
[#34996154493](https://github.com/animu-sphere/usd-3dgs-plugins/actions/runs/34996154493)
passed package-origin verification at the declared Windows L4 cap and at L5 on
macOS arm64 and Linux after the OpenUSD 26.08 re-pin.

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

Download the target archive, its `manifest.json` and `sbom.spdx.json` sidecars,
and `SHA256SUMS` from the same GitHub release. Keep those files in one
directory before extracting or activating the package. On macOS or Linux,
verify every downloaded file with:

```sh
sha256sum -c SHA256SUMS
```

On Windows PowerShell, use the equivalent check:

```powershell
Get-Content .\SHA256SUMS | ForEach-Object {
  $expected, $name = $_ -split '\s+', 2
  $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $name).Hash.ToLowerInvariant()
  if ($actual -ne $expected) {
    throw "$name has SHA-256 $actual, expected $expected"
  }
}
```

Do not extract or activate an archive when the check fails. After extraction,
confirm that the manifest describes the selected `cy2026` target, OpenUSD
profile, plugin version, and bundle names, and that the SBOM lists the same
bundle and its declared runtime closure. At minimum, validate that both
sidecars are well-formed JSON:

```powershell
Get-ChildItem -File -Include *.manifest.json,*.sbom.spdx.json |
  ForEach-Object { Get-Content $_.FullName -Raw | ConvertFrom-Json | Out-Null }
```

The checksum list covers the archive, sidecars, and source archive, but it is
not a signature. Obtain all release files from the same GitHub release page,
then use the target and ABI checks above before putting the extracted `lib`
directory on a host search path.

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
- **A large file consumes substantial memory**: v0.1 intentionally performs a
  full read/decode/author pass; streaming is future work.
