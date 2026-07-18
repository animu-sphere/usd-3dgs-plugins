# Roadmap

The roadmap contains only incomplete work. Completed work moves to the
[delivery history](../reports/delivery-history.md), and tagged behavior gets an
immutable record under [releases/](../releases/). Design rationale belongs in
[design/](../design/).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | The release-stabilization milestone and active carry-over work. |
| [backlog.md](backlog.md) | Ordered but unscheduled format and cross-cutting work. |

## Sequences

Two sequences are tracked and always qualified:

| Sequence | Notation | What it tracks | Source of truth |
| --- | --- | --- | --- |
| Delivery milestones | **M0-M5** | User-visible and engineering capability: workspace, PLY read/mapping/validation/integration, then SPZ. | [DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §18 |
| SOG maturity | **SOG M1-M4** | One SOG object through payload-oriented streaming composition. | [DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §4.4 |
| Format phases | **Phase 1-5** | PLY stabilization → SPZ → interoperability candidates → advanced loading → optional export. | [DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §4 |

Never use an unqualified “M1” for SOG work.

## Status at a glance

- M0-M4 are implemented and verified locally on Windows with OpenUSD 26.05.
- The first hosted Windows/macOS/Linux CI run and a tagged release remain open.
- M5 (`gaussian-spz`) is the next format milestone after release stabilization.
- glTF/GLB requires an ADR; SOG remains later composition work.
- The standing investment order after release stabilization is fixed by the
  [priority ladder](backlog.md#priority-ladder): real-dataset benchmarks first,
  then metadata-only reads and peak-memory reduction, then dialect
  documentation and SPZ, then bounds tightening and workaround removal.

## Quality bar

- Current-state reference docs match code and tests.
- Every claimed configuration has an observed gate, not only a generated CI job.
- New formats decode into `GaussianCloudData` and share one USD contract.
- Fixtures are deterministic, redistributable, and provenance-recorded.
- Performance work begins with measurements from license-cleared real assets.
- Every documented command has been run in the context claimed by the document.

