# Release plan

| Version | Theme | Scope |
| --- | --- | --- |
| v0.1.0–v0.10.0 | Published releases | [Release records](../releases/) |
| v0.11.0 | SPZ v4 Format Coverage | Read SPZ version 4 through the existing `gaussian-spz` bundle, after a spike and a dependency ADR. |
| v0.12.0 onward | Unscheduled | [Backlog](backlog.md) |

This repository owns import and USD authoring. See the
[API boundary](../architecture/API_BOUNDARY.md) for responsibility for integrated
distribution and consumer compatibility.

## v0.11.0 — SPZ v4 Format Coverage

### Candidate evaluation

v0.10.0 shipped SuperSplat compressed PLY and deferred the other candidates
with revisit conditions. The order below is for investigation;
[current work](current.md) tracks the selection and implementation, and the
[capability matrix](../reference/CAPABILITY_MATRIX.md) lists what the tree
actually supports.

| Order | Candidate | Check before selecting |
| --- | --- | --- |
| 1 | SPZ v4 | Confirm from primary sources that current writers emit v4 by default. Document the per-attribute ZSTD stream layout against v1-v3, find redistributable fixtures, and settle the Zstandard decoder dependency in an ADR. |
| 2 | `.splat` | Fallback with no new dependency. Identify variants and provenance, what the format preserves (it carries no SH), and a routing rule for a headerless format. |
| Research | glTF `KHR_gaussian_splatting` | Confirm the extension's ratification status and the state of its SPZ compression extension. Scope only an ADR on scene graph, external resources, and ownership with the glTF importer; no implementation in v0.11.0. |

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
