# Current: v0.12.0 — `.splat`

## Goal

Read headerless `.splat` assets, decode them into `GaussianCloudData`, and
author the stage through the shared layer writer.

## Why `.splat`

`.splat` is the fallback the [release plan](release-plan.md#candidate-evaluation)
named for v0.12.0. It needs no new dependency. It is a fixed-size record per
Gaussian with no header, written by web viewers and converters, and it
carries no spherical harmonics beyond the base colour.

## Decisions

- v0.12.0 first targeted glTF `KHR_gaussian_splatting`. FMT-09 completed its
  [spike](../reports/format-spike-gltf-gaussian-splatting-2026-11-19.md) and
  found no technical blocker. The sibling `usd-gltf-plugins` already claims
  `.gltf`/`.glb` as the primary file format and owns glTF scene translation,
  so on 2026-11-19 the maintainer deferred glTF until the two repositories
  settle ownership. Extracting the Gaussian core as a shared library is one
  option. Both items are in the [backlog](backlog.md). The glTF workstreams
  FMT-10 to FMT-12 are withdrawn and their IDs are not reused.
- `.splat` is the v0.12.0 format (selected 2026-11-19). FMT-13 decides the
  scope. If it finds a blocker, record the reason in the
  [backlog](backlog.md#other-format-candidates) before choosing another
  candidate.
- A headerless format can only be routed by extension and size, so FMT-13
  must show that the routing rule cannot claim files that belong to other
  formats or bundles.
- On 2026-11-19 the maintainer accepted the FMT-13 recommendations: a new
  `gaussian-splat` bundle with the `GSSPLAT-` prefix, and the reference
  rotation read rule `(b − 128) / 128`
  ([SPLAT_FORMAT.md §6](../reference/SPLAT_FORMAT.md#6-decisions)).

## Active work

All workstreams are owned by `usd-3dgs-plugins`. Add issue or PR links here
as work starts; keep task checklists in those issues or PRs.

| ID | Work | Exit criterion | Tracking |
| --- | --- | --- | --- |
| FMT-13 | Spike `.splat` | A report documents the record layout and its authoritative source or reference implementation; the variants in use; observed writers, their frame and value conventions, and their provenance; quantization loss (8-bit colour, opacity, and rotation; no SH); the `CanRead()` routing rule and how it coexists with other formats; redistributable fixtures; and which bundle hosts the reader. | [#76](https://github.com/animu-sphere/usd-3dgs-plugins/pull/76), [report](../reports/format-spike-splat-2026-11-19.md) |
| FMT-14 | Implement `.splat` import end to end | Reader, decoder, shared authoring, and tests pass the [format acceptance gate](release-plan.md#format-acceptance-gate), including `.splat` entries in the cross-format equivalence suite. | [#77](https://github.com/animu-sphere/usd-3dgs-plugins/pull/77) |
| FMT-15 | Update references and validate packages | Capability claims cite implementation evidence. The bundle manifest declares the `.splat` fixtures, and the declared source and package consumer gates pass on the release targets. | [#78](https://github.com/animu-sphere/usd-3dgs-plugins/pull/78) |

v0.12.0 was published on 2026-11-21; see the
[release record](../releases/v0.12.0.md).

## Release exit

- `.splat` passes the
  [format acceptance gate](release-plan.md#format-acceptance-gate).
- The [release procedure](../releases/) covers package verification, draft
  review, and the post-tag record.
