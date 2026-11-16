# Release plan

| Version | Theme | Scope |
| --- | --- | --- |
| v0.1.0–v0.9.0 | Published releases | [Release records](../releases/) |
| v0.10.0 | Format Coverage Expansion | Evaluate candidates and ship at least one additional input format, dialect, or container version that passes the acceptance gate. |
| v0.11.0 onward | Unscheduled | [Backlog](backlog.md) |

This repository owns import and USD authoring. See the
[API boundary](../architecture/API_BOUNDARY.md) for responsibility for integrated
distribution and consumer compatibility.

## v0.10.0 — Format Coverage Expansion

### Candidate evaluation

The order below is for investigation. [Current work](current.md) tracks the
selection and implementation; the [capability matrix](../reference/CAPABILITY_MATRIX.md)
lists what the tree actually supports.

| Order | Candidate | Check before selecting |
| --- | --- | --- |
| 1 | SuperSplat compressed PLY | Distinguish the dialect from ordinary Gaussian PLY by its structure. Verify specification, license, compressed attribute semantics, and fixture availability. |
| 2 | `.splat` | Identify variants and provenance. Establish what the format preserves, especially SH and coordinate conventions. |
| 3 | `.ksplat` | Identify version and compression dependencies. Check independent implementability and redistributable fixtures. |
| Research | SPZ v4 | Compare with the existing reader, assess the Zstandard dependency, and check fixtures, licensing, and maintenance cost. |

For each spike, document demand, authoritative evidence, a mapping to the
shared model, testability, and package impact. Select only the scope needed
to meet the release goal. Move candidates that are passed over to the backlog
with a reason and a trigger for reconsideration. Larger composition and
scene interoperability work is [unscheduled](backlog.md#other-format-candidates).

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
  licensed real assets where available. Verify source and installed packages
  at the declared levels on each release target.
- Update the capability matrix and the affected mapping, diagnostics, and
  usage references with the implementation evidence. Follow the
  [release gate](../releases/README.md#release-gate) before publishing.
