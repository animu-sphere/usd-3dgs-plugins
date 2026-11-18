# Current: v0.12.0 — glTF Gaussian Splatting

## Goal

Read glTF and GLB assets that store Gaussians with the ratified
`KHR_gaussian_splatting` extension, decode them into `GaussianCloudData`,
and author the stage through the shared layer writer.

## Why glTF

The [glTF / GLB policy](../design/DESIGN_POLICY.md#41-gltf--glb-gaussian-splatting)
deferred glTF until a specific extension justified an ADR. v0.11.0 kept the
extension as research only. Since then the Khronos Group has ratified
`KHR_gaussian_splatting`: the
[registry entry](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting)
reads "Complete, Ratified by the Khronos Group" (registry update of
2026-09-03, component types revised 2026-10-13). Cesium, Niantic Spatial,
Esri, Nvidia, Huawei, and Autodesk contributed to it. It would be the first
standardized Gaussian format this repository reads. The base extension
stores Gaussians as attributes of a point primitive, so it should not need a
new decoder dependency.

## Decisions

- glTF `KHR_gaussian_splatting` is the v0.12.0 target (selected
  2026-11-18). FMT-09 still has to confirm the ratified text, writers, and
  fixtures. If it or FMT-10 finds a blocker, record the reason in the
  [backlog](backlog.md#other-format-candidates) and fall back to `.splat`.
- Scope is the uncompressed base extension. No Gaussian compression
  extension is ratified: `KHR_gaussian_splatting_compression_spz_2`
  ([KhronosGroup/glTF#2531](https://github.com/KhronosGroup/glTF/pull/2531))
  and the EGSC and MPEG proposals are open pull requests.
- glTF owns a scene graph, so FMT-10 decides the questions §4.1 of the
  design policy lists before any reader code lands.
- FMT-09 found no blocker
  ([spike report](../reports/format-spike-gltf-gaussian-splatting-2026-11-19.md)).
  The values are already physical and the SH layout is Graphdeco's, so
  decoding needs a transpose and a LUF→RUB sign conversion. FMT-10 has two
  open problems. Adobe's `usdGltf` already claims `.gltf`/`.glb` as primary,
  and Sdf picks a format by extension alone (§8). Writers also use non-identity
  node transforms (§4).

## Active work

All workstreams are owned by `usd-3dgs-plugins`. Add issue or PR links here
as work starts; keep task checklists in those issues or PRs.

| ID | Work | Exit criterion | Tracking |
| --- | --- | --- | --- |
| FMT-09 | Spike glTF `KHR_gaussian_splatting` | A report documents the ratified extension: required and optional attributes, accessor component types and normalization, kernel, color space, and SH layout; the frame and unit relation between glTF and the RUB model frame; observed writers and their defaults; redistributable fixtures; and the parser cost (JSON, GLB chunks, data URIs, external buffers). | [Report](../reports/format-spike-gltf-gaussian-splatting-2026-11-19.md) |
| FMT-10 | Decide glTF ownership | An ADR settles a dedicated bundle or integration with a glTF importer, `.gltf`/`.glb` file-format registration and coexistence with other glTF plugins, which scene data is authored (node transforms, multiple primitives or nodes, non-Gaussian content), external resource resolution, and the compression extensions left out. | — |
| FMT-11 | Implement glTF Gaussian import end to end | Reader, decoder, shared authoring, and tests pass the [format acceptance gate](release-plan.md#format-acceptance-gate), including glTF entries in the cross-format equivalence suite. | — |
| FMT-12 | Update references and validate packages | Capability claims cite implementation evidence. The bundle manifest declares the glTF fixtures, and the declared source and package consumer gates pass on the release targets. Packaged notices cover any new vendored component. | — |

The remaining milestone work is the [v0.12.0 release gate](../releases/README.md#release-gate).

## Release exit

- glTF `KHR_gaussian_splatting`, or the fallback chosen by FMT-09/FMT-10,
  passes the [format acceptance gate](release-plan.md#format-acceptance-gate).
- The [release procedure](../releases/) covers package verification, draft
  review, and the post-tag record.
