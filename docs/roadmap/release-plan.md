# Release plan

| Version | Theme | Scope |
| --- | --- | --- |
| v0.1.0–v0.12.0 | Published releases | [Release records](../releases/) |
| v0.13.0 | SOG v3 and benchmark coverage | Read SOG version 3 after a spike establishes the changes from v2 and its writers, and benchmark every supported format. |
| v0.14.0 onward | Unscheduled | [Backlog](backlog.md) |

This repository owns import and USD authoring. See the
[API boundary](../architecture/API_BOUNDARY.md) for responsibility for integrated
distribution and consumer compatibility.

## v0.13.0 — SOG v3 and benchmark coverage

### Candidate evaluation

With `.splat` shipped in v0.12.0, no new format has a recorded producer and
request except SOG v3, a new version of a format this repository already
reads. The order below is for investigation; [current work](current.md)
tracks the implementation, and the
[capability matrix](../reference/CAPABILITY_MATRIX.md) lists what the tree
actually supports.

| Order | Candidate | Check before selecting |
| --- | --- | --- |
| 1 | SOG v3 | No new bundle. Identify the v2-to-v3 changes, the writers that emit v3 and whether it is their default, any new dependency, and fixtures that can be redistributed. Keep SOG v2 routing and diagnostics. |
| Deferred | glTF `KHR_gaussian_splatting` | Ownership between this repository and `usd-gltf-plugins` is undecided. See the [backlog](backlog.md#other-format-candidates). |
| Out of scope | `.ksplat` | No producer or request is recorded. See the [backlog](backlog.md#other-format-candidates). |

Benchmark coverage (PERF-01) is not a format candidate and ships in v0.13.0
whatever FMT-16 finds.

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
