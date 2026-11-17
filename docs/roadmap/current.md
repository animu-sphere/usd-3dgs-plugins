# Current: v0.11.0 — SPZ v4 Format Coverage

## Goal

Read SPZ version 4 through the existing `gaussian-spz` bundle and the shared
decoder-to-USD pipeline, so files from current SPZ writers no longer fail with
`GSPZ-E003`.

## Why SPZ v4

v0.10.0 deferred SPZ v4 because of its Zstandard dependency
([SPZ container scope](../reference/SPZ_FORMAT.md#3-container-version-scope)).
Its revisit trigger, a producer that writes v4 by default, now appears to be
met. Niantic Spatial published SPZ 4 in May
2026, replacing the single gzip stream with one ZSTD stream per attribute
class. Postshot exports v4, and the PlayCanvas writer defaults to it.
FMT-05 confirms these claims from primary sources before the release scope is
fixed. `.splat` remains the dependency-free fallback if the spike rejects v4.

## Active work

All workstreams are owned by `usd-3dgs-plugins`. Add issue or PR links here
as work starts; keep task checklists in those issues or PRs.

| ID | Work | Exit criterion | Tracking |
| --- | --- | --- | --- |
| FMT-05 | Spike SPZ v4 | A report documents the container layout and its relation to v1-v3, the authoritative specification or reference implementation, the default version of each observed writer, redistributable fixtures, and the decoder's dependency and package cost. | — |
| FMT-06 | Decide the Zstandard dependency | An ADR accepts or rejects a vendored, decompression-only Zstandard decoder: version pin, licence and notices, bounds on hostile streams, and the package change on every release target. | — |
| FMT-07 | Implement SPZ v4 end to end | Reader, decoder, shared authoring, and tests pass the [format acceptance gate](release-plan.md#format-acceptance-gate), including v4 entries in the PLY/SPZ equivalence suite. | — |
| FMT-08 | Update references and validate packages | Capability claims cite implementation evidence. The bundle manifest declares v4 fixtures, and the declared source and package consumer gates pass on the release targets. | — |

## Release exit

- SPZ v4, or the fallback chosen by FMT-05, passes the
  [format acceptance gate](release-plan.md#format-acceptance-gate).
- The [release procedure](../releases/) covers package verification, draft
  review, and the post-tag record.
