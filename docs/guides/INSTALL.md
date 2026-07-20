# Building and installing USD 3DGS Plugins

The latest tagged and published release is v0.2.0
([release records](../releases/README.md)). The verified paths are an
OpenStrata source build, a
locally generated OpenStrata package, and manual activation of an extracted
package on Windows.
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

The workspace ships two independent plugin bundles: `gaussian-ply` for
Graphdeco-style `.ply` and `gaussian-spz` for Niantic `.spz` (container
versions 1-3). They install and activate the same way — substitute
`plugins/gaussian-spz` in the per-bundle commands throughout this guide.
Install only the one you need; neither depends on the other.

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

The plugin only authors Gaussian schema data; this repository does not provide
a Hydra renderer. Opening a stage and rendering visible splats are separate
capabilities.

## Package and verify

```sh
ost plugin package plugins/gaussian-ply
ost plugin test plugins/gaussian-ply --from-package --up-to 5
```

The package is written under:

```text
plugins/<bundle>/dist/plugins/<bundle>/<version>/<target>/
```

alongside `manifest.json`, `sbom.spdx.json`, and `SHA256SUMS`.

The current package-origin run passes discovery, read, and stage-open checks
for both bundles (14 pass, 0 fail, 1 skip). L5 reports the skip because OST
does not copy the adjacent golden file into the package; source-workspace L5
passes.

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

## Plain CMake build

Point `CMAKE_PREFIX_PATH` at an OpenUSD 26.05 installation that provides a
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
