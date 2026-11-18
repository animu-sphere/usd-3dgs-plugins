# Release plan

| Version | Theme | Scope |
| --- | --- | --- |
| v0.1.0–v0.11.0 | Published releases | [Release records](../releases/) |
| v0.12.0 | `.splat` | Read headerless `.splat` assets after a spike establishes the variants, producers, frame, and a routing rule. |
| v0.13.0 onward | Unscheduled | [Backlog](backlog.md) |

This repository owns import and USD authoring. See the
[API boundary](../architecture/API_BOUNDARY.md) for responsibility for integrated
distribution and consumer compatibility.

## v0.12.0 — `.splat`

### Candidate evaluation

v0.12.0 first selected glTF `KHR_gaussian_splatting`. The FMT-09
[spike](../reports/format-spike-gltf-gaussian-splatting-2026-11-19.md) found
no technical blocker, but `.gltf` and `.glb` are already claimed as the
primary file format by the sibling `usd-gltf-plugins` importer, which also
owns glTF scene translation. On 2026-11-19 the maintainer deferred glTF until
ownership is settled. One option is to extract the Gaussian core as a library
that both plugin repositories consume. The
[backlog](backlog.md#other-format-candidates) records the trigger. `.splat`,
the planned fallback, is the v0.12.0 format. The order below is for
investigation; [current work](current.md) tracks the implementation, and the
[capability matrix](../reference/CAPABILITY_MATRIX.md) lists what the tree
actually supports.

| Order | Candidate | Check before selecting |
| --- | --- | --- |
| 1 | `.splat` | No new dependency. Identify the variants, producers, and provenance; what the format preserves (it carries no SH, and colour and rotation are 8-bit); its frame convention; and a routing rule for a headerless format that does not clash with other formats. Decide which bundle hosts it. |
| Deferred | glTF `KHR_gaussian_splatting` | Ownership between this repository and `usd-gltf-plugins` is undecided. See the [backlog](backlog.md#other-format-candidates). |
| Out of scope | `.ksplat` | No producer or request is recorded. |

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
