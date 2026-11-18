# USD 3DGS Plugins documentation

Use the document that owns the question you are answering. Summaries should
link to the source; implementation and tests are the evidence for current
behavior. If either changes, review the affected documentation in the same PR.

| Type | Owns | Start here |
| --- | --- | --- |
| Entry | Project purpose and first use | [README](../README.md) |
| Index | Navigation and document ownership | This page |
| Normative | Design principles and accepted decisions | [Design policy](design/DESIGN_POLICY.md), [ADRs](adr/) |
| Architecture | Workspace structure, dependency directions, and header audiences | [Workspace](architecture/WORKSPACE.md), [API boundary](architecture/API_BOUNDARY.md) |
| Reference | Current behavior and semantic or stage contracts | [Capability matrix](reference/CAPABILITY_MATRIX.md), [model contract](reference/GAUSSIAN_MODEL_CONTRACT.md) |
| Guide | Reproducible procedures and their required environment | [User guides](guides/), [contributor guides](contributing/) |
| Plan | Release criteria and open work | [Roadmap](roadmap/) |
| Record | Published releases, measured results, and completed work | [Release records](releases/), [reports](reports/), [changelog](../CHANGELOG.md) |

## Where status belongs

| Question | Source of truth |
| --- | --- |
| What does the tree support now? | [Capability matrix](reference/CAPABILITY_MATRIX.md). See the linked reference and tests for exact behavior. |
| What does the next release aim to ship? | [Release plan](roadmap/release-plan.md). |
| What work is assigned to the next release? | [Current work](roadmap/current.md), with issues or PRs for task-level status. |
| What remains unscheduled? | [Backlog](roadmap/backlog.md), with an owner and a reason or trigger to revisit each item. |
| What shipped and what passed for a tag? | Its [release record](releases/). The changelog describes user-visible changes. |
| What performance was measured? | [Performance baselines](reference/PERFORMANCE_BASELINES.md) and [benchmark corpus results](reference/BENCHMARK_CORPUS.md). |
| What did an integrated host or renderer verify? | The Formation owner's evidence. The [API boundary](architecture/API_BOUNDARY.md) describes repository ownership and the [renderer note](reference/CAPABILITY_MATRIX.md#hydra-rendering) links to the verified example. |

## Maintenance

- Agree on changes to normative rules during review; record a new ADR when a
  decision changes.
- Update references with the implementation and tests that support the claim.
  Keep plans and version promises out of reference descriptions.
- Keep procedures reproducible and name the relevant runtime and platform in
  the guide. See [Contributing](../CONTRIBUTING.md#documentation-review).
- Move finished work out of the roadmap. Keep release records and measured
  results as evidence for their original version; use an erratum for a
  factual correction.
- Route evidence from other repositories to its owner and link to a pinned
  record when available.

## Component documentation

| Component | Documentation |
| --- | --- |
| `gaussian-ply` | [Plugin README](../plugins/gaussian-ply/README.md) |
| `gaussian-spz` | [Plugin README](../plugins/gaussian-spz/README.md) |
| `gaussian-sog` | [Plugin README](../plugins/gaussian-sog/README.md) |
| `gaussian-splat` | [Plugin README](../plugins/gaussian-splat/README.md) |
| `gaussianCore` | [Library README](../libs/gaussian-core/README.md) |
| `gaussianUsd` | [Library README](../libs/gaussian-usd/README.md) |
