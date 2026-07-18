# Reports

Reports contain historical evidence from real runs. They record what was
observed on a specific tool version, runtime, host, and date; they are not
current-state contracts.

| Document | Contents |
| --- | --- |
| [ost/](ost/) | Append-only OpenStrata dogfooding reports. |
| [delivery-history.md](delivery-history.md) | Completed milestone and capability log. |

## What belongs where

- Current structure belongs in [architecture/](../architecture/).
- Current behavior belongs in [reference/](../reference/).
- Design rationale belongs in [design/](../design/).
- Incomplete work belongs in [roadmap/](../roadmap/).
- Tagged behavior belongs in [releases/](../releases/).
- The root [CHANGELOG.md](../../CHANGELOG.md) summarizes user-visible changes.

When a report disagrees with a current-state document, the current-state
document wins and the report remains unchanged as history. A later report may
add a forward-note or correction, but the original measurement is not rewritten.

