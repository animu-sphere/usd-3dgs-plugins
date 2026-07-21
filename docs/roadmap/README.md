# Roadmap

The roadmap contains only incomplete work. Completed work moves to the
[delivery history](../reports/delivery-history.md), and tagged behavior gets an
immutable record under [releases/](../releases/). Design rationale belongs in
[design/](../design/).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [release-plan.md](release-plan.md) | The versioned release sequence (v0.1.0 → v1.0.0) with per-release themes and completion criteria. |
| [current.md](current.md) | The active v0.4.0 task breakdown and carry-over work. |
| [backlog.md](backlog.md) | Ordered but unscheduled format and cross-cutting work. |

## Sequences

Four sequences are tracked and always qualified:

| Sequence | Notation | What it tracks | Source of truth |
| --- | --- | --- | --- |
| Release versions | **v0.1.0-v1.0.0** | One tagged release per primary theme, from the PLY vertical slice to the stable import API. | [release-plan.md](release-plan.md) |
| Delivery milestones | **M0-M5** | User-visible and engineering capability: workspace, PLY read/mapping/validation/integration, then SPZ. | [DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §18 |
| SOG maturity | **SOG M1-M4** | One SOG object through payload-oriented streaming composition. | [DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §4.4 |
| Format phases | **Phase 1-5** | PLY stabilization → SPZ → interoperability candidates → advanced loading → optional export. | [DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §4 |

Never use an unqualified “M1” for SOG work.

## Status at a glance

- v0.1.0, v0.2.0, and v0.3.0 are tagged and published with immutable
  [release records](../releases/README.md); completed milestone detail is in
  the [delivery history](../reports/delivery-history.md).
- The current development target is **v0.4.0 — Gaussian Import Foundation**
  ([release-plan.md](release-plan.md)): formalize the shared decoder-to-USD
  contract (design policy §7.4) — a normative model contract, shared semantic
  validation, a coordinate-system ADR, and a decoder test kit — before a third
  format depends on it.
- M5 (`gaussian-spz`) shipped in v0.3.0: SPZ decodes into `GaussianCloudData`
  and reuses the shared USD authoring contract; a second USD authoring path is
  not permitted, and making that invariant normative is v0.4.0's purpose.
- v0.5.0 imports one SOG v2 object (SOG M1) through the same contract; glTF/GLB
  and other Phase 3 formats are reconsidered only after SOG v2 ships.
- The v0.9.0 rendering integration preview is delivered by the sibling project
  [hydra-merlin](https://github.com/animu-sphere/hydra-merlin); this
  repository stays renderer-neutral.
- Of the [priority ladder](backlog.md#priority-ladder), P0 (real-dataset
  benchmarks), P1 (metadata-only reads), and P2 (PLY dialects and the SPZ
  importer) have shipped; P3 stays unscheduled.

## Quality bar

- Current-state reference docs match code and tests.
- Every claimed configuration has an observed gate, not only a generated CI job.
- New formats decode into `GaussianCloudData` and share one USD contract.
- Fixtures are deterministic, redistributable, and provenance-recorded.
- Performance work begins with measurements from license-cleared real assets.
- Every documented command has been run in the context claimed by the document.

