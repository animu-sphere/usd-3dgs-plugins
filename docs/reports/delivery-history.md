# Delivery history

This is the granular log of completed work. It was separated from the
[roadmap](../roadmap/) so the roadmap contains only incomplete items.

No release has been tagged yet. Everything below describes the current
pre-release tree and was exercised locally on Windows against OpenUSD 26.05.

Legend: ✅ completed locally

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

Hosted CI, clean external extraction, and the first tag remain incomplete and
therefore stay in [roadmap/current.md](../roadmap/current.md).
