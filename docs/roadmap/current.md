# Current: v0.10.0 — Format Coverage Expansion

## Goal

Extend input coverage through the shared decoder-to-USD pipeline.

## Scope and selection

FMT-02 selected SuperSplat compressed PLY as the v0.10.0 input, scoped as in
[§7 of the spike](../reports/format-spike-compressed-ply-2026-11-17.md#7-proposed-scope):

- Binary little-endian, the 18-property `chunk`, exactly the four packed
  `uint` vertex properties, and an optional `sh` element for SH degree 1–3.
  Full and metadata-only reads.
- The 12-property chunk, ASCII, and big-endian encodings are rejected with
  specific `GSPLY` diagnostics.
- Routing and the decoder stay inside the existing `gaussian-ply` bundle. No
  new dependency, library, or registration.

`.splat`, `.ksplat`, and SPZ v4 were not selected; the
[backlog](backlog.md#other-format-candidates) records why and when to revisit
them. The [capability matrix](../reference/CAPABILITY_MATRIX.md) is the
reference for what can be opened today.

## Active work

All workstreams are owned by `usd-3dgs-plugins`. Add issue or PR links here
as work starts; keep task checklists in those issues or PRs.

| ID | Work | Exit criterion | Tracking |
| --- | --- | --- | --- |
| FMT-01 | Evaluate candidates, starting with compressed PLY | Spike documents representative assets, a reproducible format signature, attribute fidelity, license and fixture availability, and dependency cost. | [Compressed PLY spike](../reports/format-spike-compressed-ply-2026-11-17.md) |
| FMT-02 | Select the release scope | Record the selected format or variant in the selection issue or PR. Record skipped candidates and revisit conditions in the [backlog](backlog.md). | Compressed PLY selected; see [Scope and selection](#scope-and-selection) |
| FMT-03 | Implement the first selected format end to end | Reader, decoder, shared authoring, and tests pass the [format acceptance gate](release-plan.md#format-acceptance-gate). | [PR #63](https://github.com/animu-sphere/usd-3dgs-plugins/pull/63); [mapping contract](../reference/COMPRESSED_PLY_MAPPING.md) |
| FMT-04 | Update references and validate packages | Capability claims cite implementation evidence. Declared source and package consumer gates pass on the release targets. | FMT-04 PR (link on open) |

## Decisions

- Render-mode tags warn. A non-default `SplatRenderMode` (`mip` or `2dgs`) or
  `postshot.anti_aliasing=1` in a compressed PLY produces `GSPLY-W004`, by
  analogy with SPZ's antialiased flag (`GSPZ-W002`). The
  [backlog](backlog.md#other-format-candidates) records whether the
  uncompressed dialect should follow.
- The source-format label is `"Gaussian Splatting compressed PLY"` in
  `customData.gs` and in the import stats, so consumers can tell a quantized
  source apart. Uncompressed PLY keeps `"Gaussian Splatting PLY"`.

## Release exit

- At least one selected input passes the
  [format acceptance gate](release-plan.md#format-acceptance-gate).
- The [release procedure](../releases/) covers package verification, draft
  review, and the post-tag record.
