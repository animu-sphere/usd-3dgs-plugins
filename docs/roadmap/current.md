# Current: v0.13.0 — SOG v3 and benchmark coverage

## Goal

Read SOG version 3 through the existing `gaussian-sog` bundle and the shared
decoder-to-USD pipeline, keeping SOG v2 support. Extend the benchmark corpus
to every supported format, so the performance targets cover `.splat` and
SOG v3 as well as PLY, SPZ, and SOG v2.

## Why SOG v3

The PlayCanvas SOG specification now describes version 3, with `meta.json`
declaring `version: 3`, and SplatTransform writes it. The `gaussian-sog`
reader accepts only `version == 2` and keeps `version-3.sog` as a negative
fixture, so files from the current reference converter fail to open. FMT-16
confirms from primary sources what changed between v2 and v3 and which
writers emit v3 by default before the release scope is fixed.

## Why benchmark coverage

v0.12.0 shipped `.splat` without benchmark rows
([backlog entry, now scheduled](backlog.md#performance-and-loading)). The
corpus, scale, and hosted-gate tiers cover PLY, SPZ, and SOG v2 only, so the
[performance targets](../reference/BENCHMARK_CORPUS.md#targets) say nothing
about `.splat`, and would say nothing about SOG v3 either.

## Decisions

- SOG v3 and benchmark coverage are the v0.13.0 scope (selected 2026-11-21).
  If FMT-16 finds a blocker, record the reason in the
  [backlog](backlog.md#other-format-candidates); PERF-01 still ships.
- Streamed SOG and LOD stay in the backlog. FMT-16 records whether v3
  changes that boundary.
- A second OpenUSD SDK point was considered and not scheduled: the
  OpenStrata runtime matrix and the sibling `usd-vrm-plugins` pin only
  OpenUSD 26.08, and the 26.05 runtimes have been withdrawn
  ([backlog](backlog.md#package-verification)).

## Active work

All workstreams are owned by `usd-3dgs-plugins`. Add issue or PR links here
as work starts; keep task checklists in those issues or PRs.

| ID | Work | Exit criterion | Tracking |
| --- | --- | --- | --- |
| FMT-16 | Spike SOG v3 | A report documents the v2-to-v3 changes from the specification and the SplatTransform source; which writers and versions emit v3 and whether it is their default; bundled and unbundled layouts; frame and value conventions; any new dependency or codec; the new hostile-input surface; redistributable fixtures; and what replaces the `version-3.sog` negative fixture. | — |
| FMT-17 | Implement SOG v3 import end to end | Reader, decoder, shared authoring, and tests pass the [format acceptance gate](release-plan.md#format-acceptance-gate), including SOG v3 entries in the cross-format equivalence suite. SOG v2 behaviour and diagnostics are unchanged. | — |
| FMT-18 | Update references and validate packages | Capability claims cite implementation evidence. The bundle manifest declares the SOG v3 fixtures, and the declared source and package consumer gates pass on the release targets. | — |
| PERF-01 | Benchmark every supported format | The fixture, corpus, and scale tiers and the hosted gate include `.splat` and SOG v3 rows generated from the same CC0 sources as the existing rows. [BENCHMARK_CORPUS.md](../reference/BENCHMARK_CORPUS.md) records the new baseline, and every row meets the targets or the targets are revised with a stated reason. | — |

## Release exit

- SOG v3 passes the
  [format acceptance gate](release-plan.md#format-acceptance-gate).
- The benchmark targets hold for every supported format.
- The [release procedure](../releases/) covers package verification, draft
  review, and the post-tag record.
