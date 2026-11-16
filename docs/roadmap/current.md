# Current: v0.10.0 — Format Coverage Expansion

## Goal

Extend input coverage through the shared decoder-to-USD pipeline.

## Scope and selection

Use the [candidate evaluation order](release-plan.md#candidate-evaluation) to
start the spikes. Release scope is pending evidence review. The
[capability matrix](../reference/CAPABILITY_MATRIX.md) is the reference for
what can be opened today.

## Active work

All workstreams are owned by `usd-3dgs-plugins`. Add issue or PR links here
as work starts; keep task checklists in those issues or PRs.

| ID | Work | Exit criterion | Tracking |
| --- | --- | --- | --- |
| FMT-01 | Evaluate candidates, starting with compressed PLY | Spike documents representative assets, a reproducible format signature, attribute fidelity, license and fixture availability, and dependency cost. | — |
| FMT-02 | Select the release scope | Record the selected format or variant in the selection issue or PR. Record skipped candidates and revisit conditions in the [backlog](backlog.md). | — |
| FMT-03 | Implement the first selected format end to end | Reader, decoder, shared authoring, and tests pass the [format acceptance gate](release-plan.md#format-acceptance-gate). | — |
| FMT-04 | Update references and validate packages | Capability claims cite implementation evidence. Declared source and package consumer gates pass on the release targets. | — |

## Decisions needed

- FMT-01 owner: verify that compressed PLY can route unambiguously alongside
  the existing PLY path and that representative fixtures can be published.
- FMT-02 owner: accept a format only after identifying any lossy or missing
  semantics and confirming the scope can ship independently.

## Release exit

- At least one selected input passes the
  [format acceptance gate](release-plan.md#format-acceptance-gate).
- The [release procedure](../releases/) covers package verification, draft
  review, and the post-tag record.
