# Release plan

| Version | Theme | Scope |
| --- | --- | --- |
| v0.1.0–v0.11.0 | Published releases | [Release records](../releases/) |
| v0.12.0 | glTF Gaussian Splatting | Read glTF and GLB assets that use the ratified `KHR_gaussian_splatting` extension, after a spike and an ownership ADR. |
| v0.13.0 onward | Unscheduled | [Backlog](backlog.md) |

This repository owns import and USD authoring. See the
[API boundary](../architecture/API_BOUNDARY.md) for responsibility for integrated
distribution and consumer compatibility.

## v0.12.0 — glTF Gaussian Splatting

### Candidate evaluation

v0.11.0 shipped SPZ v4. Its research item, the status of glTF
`KHR_gaussian_splatting`, is settled: the extension is marked "Complete,
Ratified by the Khronos Group" in the
[glTF extension registry](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting)
since 2026-09-03. The order below is for investigation;
[current work](current.md) tracks the selection and implementation, and the
[capability matrix](../reference/CAPABILITY_MATRIX.md) lists what the tree
actually supports.

| Order | Candidate | Check before selecting |
| --- | --- | --- |
| 1 | glTF `KHR_gaussian_splatting` (base extension, uncompressed) | Confirm the ratified text, the attribute and accessor rules, the kernel and color-space defaults, and the frame relation between glTF (+Y up, right-handed, metres) and the RUB model frame. Find writers and redistributable fixtures. Settle in an ADR the bundle and `.gltf`/`.glb` file-format ownership, node transforms and multiple primitives, external buffers, and coexistence with other glTF file-format plugins. |
| 2 | `.splat` | Fallback with no new dependency if the spike or ADR rejects glTF for this release. Identify variants and provenance, what the format preserves (it carries no SH), and a routing rule for a headerless format. |
| Out of scope | glTF Gaussian compression extensions | None is in the Khronos registry. `KHR_gaussian_splatting_compression_spz_2` ([#2531](https://github.com/KhronosGroup/glTF/pull/2531)), EGSC, and MPEG proposals are open pull requests. Revisit after one is ratified. |

For each spike, document demand, authoritative evidence, a mapping to the
shared model, testability, and package impact. Select only the scope needed
to meet the release goal. Move candidates that are passed over to the backlog
with a reason and a trigger for reconsideration.

### Format acceptance gate

- Document the specification or independently implementable reference,
  licensing, asset provenance, and supported and rejected variants.
- Separate cheap, structural `CanRead()` routing from `Read()` validation.
  Preserve routing and diagnostics for formats already supported.
- Use a format reader and semantic decoder to produce `GaussianCloudData`;
  author the stage through the shared `GaussianLayerWriter`.
- Document coordinate and unit policy, SH, opacity, scale, and orientation
  semantics. State any missing information or quantization loss.
- Enforce bounds and diagnostics for malformed or hostile input. Verify the
  metadata-only behavior available for the format.
- Add deterministic fixtures, quantization-aware equivalence tests, and
  licensed real assets where available. Declare the new fixtures in the
  bundle manifest and the aggregate package consumer test, so source and
  installed packages are verified with them at the declared levels on each
  release target.
- Update the capability matrix and the affected mapping, diagnostics, and
  usage references with the implementation evidence. Follow the
  [release gate](../releases/README.md#release-gate) before publishing.
