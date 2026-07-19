# Delivery history

This is the granular log of completed work. It was separated from the
[roadmap](../roadmap/) so the roadmap contains only incomplete items.

Sections A-E describe the pre-release tree and were exercised locally on
Windows against OpenUSD 26.05. The first tagged release is
[v0.1.0](../releases/v0.1.0.md); later sections record post-tag stabilization
work.

Legend: ✅ completed (sections A-G locally on Windows; section H on hosted
runners or GitHub)

## A. M0 — workspace bootstrap

- ✅ Initialized `usd-3dgs-plugins` from OpenStrata 0.18.0's
  `usd-plugin-workspace` scaffold.
- ✅ Created the `gaussian-ply` `usd-fileformat` bundle and `.ply` registration.
- ✅ Added a dual-mode root CMake build that discovers future `plugins/*`
  bundles dynamically.
- ✅ Exercised the plain root build with Visual Studio 2022: both components
  built and all three composed CTest entries passed.
- ✅ Added `gaussianCore` as an OpenStrata plain library with a versioned CMake
  package and the declared `gaussian-ply -> gaussianCore` workspace edge.
- ✅ Generated and validated a three-host PR CI matrix using immutable runtime
  and OCI digests.

## B. M1 — minimal PLY read

- ✅ Vendored tinyPLY 2.3.4 at commit
  `40aa4a0ae9e9c203e11893f78b8bcaf8a50e65f0` with retained upstream notice.
- ✅ Isolated tinyPLY behind `PlyReader`.
- ✅ Added ASCII and binary little-endian fixtures.
- ✅ Read the vertex count and positions and opened the result through the real
  OpenUSD file-format path.
- ✅ Established `/Asset/Splat`, `/Asset` default prim, Y-up, and one meter per
  unit.

## C. M2 — Gaussian semantic mapping

- ✅ Converted logarithmic scales with `exp`.
- ✅ Converted opacity logits with a numerically stable sigmoid.
- ✅ Parsed scalar-first quaternions, normalized them, and repaired zero length
  to identity with a warning.
- ✅ Reconstructed Graphdeco channel-major SH data into coefficient-major RGB
  values and inferred the degree from a square coefficient count.
- ✅ Authored OpenUSD 26.05's standard
  `ParticleField3DGaussianSplat` attributes.
- ✅ Authored conservative three-sigma extent and source/count/degree metadata.

## D. M3 — validation

- ✅ Rejected valid non-Gaussian mesh PLY, missing required properties, invalid
  SH layouts, unsupported required property forms, non-finite values, array
  length disagreement, and truncated binary input.
- ✅ Aggregated unknown-property warnings and silently ignored common normal
  placeholders.
- ✅ Kept `WriteToFile` unsupported and the bundle read-only.

## E. M4 — integration quality

- ✅ Added `gaussianCore` math/model C++ tests.
- ✅ Added parser/decoder C++ tests for ASCII, binary, SH layout, and failures.
- ✅ Added Python end-to-end stage assertions for hierarchy, metadata, schema,
  values, and negative inputs.
- ✅ Added an OST L5 golden fixture.
- ✅ Passed local CTest: one `gaussianCore` test and two plugin tests.
- ✅ Passed local `ost plugin test --workspace --up-to 5`: 12 pass, 0 fail,
  3 expected skips.
- ✅ Generated a 20-file package with bundled third-party notice and digest
  `sha256:992863defe60b96edb91343ecee6666d5e2f84aa1f12809c36f0d16fe38cd997`.
- ✅ Passed package-origin verification: 14 pass, 0 fail, 1 L5 golden skip.
- ✅ Added the English documentation taxonomy, workspace contract, mapping
  reference, support matrix, roadmap, release structure, and dogfooding log.

## F. v0.1 stabilization — packaged-consumer closure and Windows reproducibility

Post-v0.1.0 work, exercised locally on Windows on 2026-07-19.

- ✅ Extracted the Windows package to a clean directory outside the worktree,
  verified all 20 files against the package manifest hashes, and opened the
  ASCII, binary little-endian, and degree-1 SH fixtures from there via
  `ost plugin run <extracted-root>`. `Plug.Registry` resolved the plugin DLL
  from the extracted tree, not the build tree.
- ✅ Verified manual OpenUSD activation on Windows without `ost` in the launch
  path, for both a `usdcat` host and a plain Python host. Found and documented
  that `usdcat` embeds Python and silently exits when `python313.dll` is not
  resolvable; found that the extracted `lib` directory is not needed on the
  loader path because `plugInfo.json` resolves `LibraryPath` absolutely, and
  that the Python host needed no `os.add_dll_directory()` call.
- ✅ Identified both causes of the Windows across-run package digest drift.
  Repackaging the same build is byte-stable; a clean relink changed exactly the
  PE-header and debug-directory timestamps; and the July-18 local package had
  silently captured the plain Visual Studio build's DLL because the dual-mode
  root build writes into the same bundle `lib/` directory that `ost plugin
  package` stages.
- ✅ Made MSVC output reproducible with `/Brepro` on `cl.exe`, `lib.exe`, and
  `link.exe` for `gaussianCore` and the plugin. Two fully clean local
  build+package cycles now produce identical archive digests; the workspace
  (12 pass, 0 fail, 3 skip), package-origin (14 pass, 0 fail, 1 skip), and
  CTest (3/3) baselines are unchanged.

The reproducibility fix was later observed on hosted runners (section H);
the Windows L5 decision and the macOS across-run investigation stay in
[roadmap/current.md](../roadmap/current.md).

## G. Read-path memory-copy reduction

Post-v0.1.0 work, exercised locally on Windows on 2026-07-19 against
OpenUSD 26.05.

- ✅ `PlyReader` now converts vertex properties directly to `float` instead of
  `double` (out-of-float-range doubles become ±infinity so the decoder's
  finiteness validation still rejects them) and releases each tinyPLY native
  buffer as soon as its column is converted.
- ✅ `GaussianPlyDecoder` decodes column-wise: each property column is moved
  out of the `PlyDocument` and freed as it is converted into
  `GaussianCloudData`, replacing the per-row map-lookup loop.
- ✅ `GaussianLayerWriter` consumes the cloud (`WriteToLayer` takes it by
  rvalue), releases each source array once copied into its copy-on-write
  `VtArray` attribute, and returns the authored anonymous layer directly.
- ✅ `GaussianPlyFileFormat::Read` transfers the worker-authored layer with
  `TransferContent`, removing the USDA string serialization and reparse
  round-trip. The worker-thread `SdfChangeBlock` workaround (§7.5) is
  unchanged.
- ✅ Regenerated the L5 golden: attribute values are identical; the layer
  `doc` metadata no longer embeds the doubled round-trip provenance line.
- ✅ Measured on local Postshot captures (SH degree 3, not redistributable),
  peak commit / `Usd.Stage.Open` wall time, before → after:
  139k Gaussians: 1.70 GiB / 3.1 s → 0.18 GiB / 0.4 s;
  696k Gaussians: 8.05 GiB / 15.3 s → 0.40 GiB / 1.9 s;
  1.94M Gaussians (after only): 0.92 GiB / 5.2 s.
- ✅ Baselines unchanged: workspace `ost plugin test --up-to 5` 12 pass /
  0 fail / 3 skip, CTest 2/2 plugin tests pass.
- ✅ Review follow-up: added the `out-of-range-double.ply` negative fixture so
  the double→float narrowing rejection (out-of-range doubles become ±infinity
  and fail finiteness validation) is regression-tested, and `takeColumn` now
  reports which property is missing or size-mismatched instead of the generic
  non-finite-value error. Rebuilt and re-ran after the fixes: ladder 12 pass /
  0 fail / 3 skip with 7 fixtures, CTest 2/2.

## H. v0.1/v0.2 release stabilization — hosted evidence and the v0.2.0 release

Work observed on hosted runners or on GitHub, closed with the v0.2.0
publication on 2026-07-19.

- ✅ Ran the generated Windows 2022, macOS 15 arm64, and Ubuntu 24.04 PR cells
  (first observed on PR #1) and recorded compiler/runtime/package digests in
  the [release records](../releases/README.md).
- ✅ Exercised the extracted package from a clean directory outside the
  worktree (manifest-verified extraction, `ost plugin run <extracted-root>`)
  and verified manual OpenUSD activation on Windows for both a `usdcat` host
  and a plain Python host
  ([INSTALL.md](../guides/INSTALL.md#manual-package-activation)).
- ✅ Added the tag-driven release lane
  ([release.yml](../../.github/workflows/release.yml) driven by
  [scripts/release.py](../../scripts/release.py)), deriving its matrix from
  `openstrata.ci.yaml`; exercised by dry runs and the v0.1.0 and v0.2.0 tags.
- ✅ Observed across-run package reproducibility on hosted runners: the v0.2.0
  dry run and tag run (same commit `7323f46`, separate runners) produced
  byte-identical Windows and Linux `tar.zst` archives, the first hosted
  confirmation of the `/Brepro` fix. macOS remains open, tracked in
  [roadmap/current.md](../roadmap/current.md).
- ✅ Selected and committed the CC0-1.0
  [yashica-t4 corpus](../../plugins/gaussian-ply/tests/corpus/yashica-t4/PROVENANCE.md)
  (author-captured, Brush v0.3.0, SH degree 3, 8,192 Gaussians, deterministic
  derivation via `scripts/ply_subset.py`) and recorded the design-policy
  §12.1 baselines across six assets from 3 to 5.83M Gaussians
  ([PERFORMANCE_BASELINES.md](../reference/PERFORMANCE_BASELINES.md)).
- ✅ Shipped v0.2.0 — production-ready Graphdeco PLY import: metadata-only
  reads, file-format arguments, stable `GSPLY-****` diagnostics with a
  machine-readable catalog, degree-2/3 and malformed fixtures, observed
  dialect compatibility, and release-version single-sourcing. PR cells green
  on PR #11, dry run green, tagged 2026-07-19 (`7323f46`).
- ✅ Reviewed and published the v0.1.0 and v0.2.0 GitHub releases 2026-07-19;
  both [release records](../releases/README.md) record the published state.
