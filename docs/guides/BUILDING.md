# Building and testing

The full build, test, and packaging surface for this workspace. The
30-second version is in the top-level [README](../../README.md#quick-start);
installing a packaged release is in [INSTALL.md](INSTALL.md).

## OpenStrata path (primary)

Requirements: `ost` 0.18.0 (verified baseline), a real `cy2026` / `usd`
OpenStrata runtime, OpenUSD `>=26.05,<27.0`, a C++17 compiler. The verified
local runtime is OpenUSD 26.05 on Windows x86-64 / MSVC 143 / Python 3.13.

```sh
ost runtime pull cy2026 --profile usd
ost plugin build plugins/gaussian-ply
ost plugin doctor plugins/gaussian-ply
ost plugin test plugins/gaussian-ply --up-to 5
ost plugin test --workspace --up-to 5
ost plugin package plugins/gaussian-ply
```

`openstrata.ci.yaml` is the cross-platform CI contract. Regenerate the
checked-in GitHub Actions workflows after matrix changes:

```sh
ost ci generate github --force
```

### Viewing and converting assets

```powershell
ost plugin view plugins\gaussian-ply "C:\path\to\scene.ply"
ost plugin run plugins\gaussian-ply -- usdcat --flatten --skipSourceFileComment --usdFormat usdc --out scene.usd scene.ply
```

The stage can be inspected even when the active Hydra renderer does not draw
Gaussian splats.

## CTest coverage

The repository carries CTest coverage beyond OST's verification pyramid:

```sh
ctest --test-dir libs/gaussian-core/build/cy2026-windows-x86_64-py313-usd --output-on-failure
ctest --test-dir plugins/gaussian-ply/build/cy2026-windows-x86_64-py313-usd --output-on-failure
```

## Plain CMake path (no ost)

The repo is dual-mode: everything also builds with plain CMake against any
OpenUSD 26.05 installation.

```sh
cmake --preset default -DCMAKE_PREFIX_PATH=/path/to/openusd
cmake --build --preset default
ctest --test-dir build/default --output-on-failure
```

Note for OST users: the plain root build writes its plugin DLL into the same
bundle `lib/` directory as `ost plugin build`, overwriting that flavor
(recorded in [dogfooding report 02](../reports/ost/02-2026-07-19-package-provenance-and-reproducibility.md)).

## Release lane

Pushing a tag `vX.Y.Z` (agreeing with [`VERSION`](../../VERSION), every
bundle manifest, and a finalized `CHANGELOG.md` section — enforced by
`scripts/release.py guard`, maintained by `scripts/release.py set-version`)
runs [`release.yml`](../../.github/workflows/release.yml): it builds and
verifies on the same three digest-pinned cells as the PR lane, proves
packaging is digest-reproducible, and assembles a **draft** GitHub release —
per-target lean bundles with manifest and SBOM sidecars, a source archive,
`SHA256SUMS`, and notes rendered from `CHANGELOG.md`. Publishing the draft is
a human decision. Run the lane manually (`workflow_dispatch`) for a dry run
that creates no release. Details: [release records](../releases/README.md).
