# Reports

Reports contain historical evidence from real runs. They record what was
observed on a specific tool version, runtime, host, and date; they are not
current-state contracts.

| Document | Contents |
| --- | --- |
| [ost/](ost/) | Append-only OpenStrata dogfooding reports. |
| [delivery-history.md](delivery-history.md) | Completed milestone and capability log. |
| [Roadmap triage (2026-11-17)](roadmap-triage-2026-11-17.md) | Disposition and evidence for stale roadmap items. |
| [Compressed PLY spike (2026-11-17)](format-spike-compressed-ply-2026-11-17.md) | FMT-01 evaluation of SuperSplat compressed PLY against pinned upstream code and real files. |
| [SPZ v4 spike (2026-11-17)](format-spike-spz-v4-2026-11-17.md) | FMT-05 evaluation of the SPZ v4 container, writer defaults, frame risks, fixtures, and Zstandard cost. |
| [glTF Gaussian splatting spike (2026-11-19)](format-spike-gltf-gaussian-splatting-2026-11-19.md) | FMT-09 evaluation of the ratified `KHR_gaussian_splatting` extension: attributes, writers, fixtures, parser cost, and file-format coexistence. Its frame analysis is corrected in the report. |
| [`.splat` spike (2026-11-19)](format-spike-splat-2026-11-19.md) | FMT-13 evaluation of the headerless `.splat` format: record layout, writer arithmetic, frame, quantization loss, routing, fixtures, and the hosting bundle. |
| [SOG v3 spike (2026-11-21)](format-spike-sog-v3-2026-11-21.md) | FMT-16 evaluation of SOG version 3: changes from v2, writer versions and the interim layout, fit with the shared model, routing, fixtures, and the decisions FMT-17 needs. |

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

