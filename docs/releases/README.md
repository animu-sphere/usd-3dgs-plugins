# Release records

Each tagged version receives an immutable record here: objective, shipped
capabilities, compatibility, artifact names and digests, and known limitations.
Release records are history and are not rewritten after publication.

| Version | Date | Record |
| --- | --- | --- |
| v0.11.0 | 2026-11-18 | [v0.11.0.md](v0.11.0.md) — SPZ v4 format coverage |
| v0.10.0 | 2026-11-17 | [v0.10.0.md](v0.10.0.md) — format coverage expansion: SuperSplat compressed PLY |
| v0.9.0 | 2026-11-16 | [v0.9.0.md](v0.9.0.md) — experimental rendering integration preview |
| v0.8.0 | 2026-11-15 | [v0.8.0.md](v0.8.0.md) — USD packaging and asset composition |
| v0.7.0 | 2026-11-15 | [v0.7.0.md](v0.7.0.md) — performance and large-asset readiness |
| v0.6.0 | 2026-11-13 | [v0.6.0.md](v0.6.0.md) — production import hardening and aggregate package verification |
| v0.5.1 | 2026-09-16 | [v0.5.1.md](v0.5.1.md) — release polish, package verification, and hostile-input hardening; released |
| v0.5.0 | 2026-07-25 | [v0.5.0.md](v0.5.0.md) — SOG v2 one-object import, third-format equivalence, and OST 0.20.0 aggregate product packaging; released |
| v0.4.0 | 2026-07-22 | [v0.4.0.md](v0.4.0.md) — Gaussian Import Foundation: the decoder-to-USD seam formalized, RUB frame (ADR 0001), decoder test kit, and the `gaussian-sog` skeleton; published 2026-07-22 |
| v0.3.0 | 2026-07-20 | [v0.3.0.md](v0.3.0.md) — read-only SPZ import through the shared pipeline; published 2026-07-20 |
| v0.2.0 | 2026-07-19 | [v0.2.0.md](v0.2.0.md) — production-ready Graphdeco PLY import; published 2026-07-19 |
| v0.1.0 | 2026-07-19 | [v0.1.0.md](v0.1.0.md) — first tagged release; published 2026-07-19 |

After the tag workflow and publication, add the record and its index row in
the same PR. Record the tagged commit, consumed runtime digests, verification
results, and published artifact checksums. Link external Formation evidence
from the owner repository.

## Release gate

A release record is created only after:

1. `VERSION`, tag, and finalized changelog version agree;
2. every declared hosted CI cell passes;
3. source and package-origin gates pass at their documented levels;
4. package digests are reproducible for an unchanged build;
5. notices, SBOM/provenance policy, and target metadata are verified;
6. the release is assembled as a draft for human review.

Use [RELEASE_NOTES_TEMPLATE.md](../contributing/RELEASE_NOTES_TEMPLATE.md) for
the release body.

## How the gate runs

Pushing a `vX.Y.Z` tag starts
[release.yml](../../.github/workflows/release.yml), which enforces items 1–4
and 6 automatically:

| Gate item | Enforced by |
| --- | --- |
| 1. version and record agreement | `scripts/release.py guard` checks the tag, `VERSION`, bundle versions, and finalized changelog heading. `scripts/check_docs.py --release-tag` checks the tag against `VERSION` and checks the indexed records for earlier tags reachable from the release commit. |
| 2. hosted cells pass | one job per source cell in `openstrata.ci.yaml`, at that cell's `up_to` level |
| 3. source and installed packages | `ost plugin test` from source and from package at each cell's declared level, plus the [aggregate installation check](../guides/INSTALL.md#verify-the-aggregate-installation) |
| 4. reproducible digests | the lane packages twice and fails on disagreeing `archive_digest` |
| 6. draft for review | `gh release create --draft`; publishing stays a human action |

The workflow takes its runtime digests, `ost` version, and per-cell levels from
`openstrata.ci.yaml` rather than restating them, so re-pinning a runtime moves
the PR and release lanes together.

The tag being built is exempt from the missing-record check: its artifacts
and results do not exist in the tagged tree yet. PR and dry-run docs checks
validate the record index and links already present without requiring a record
for the next version.

Run the lane manually (`workflow_dispatch`) for a dry run: it builds, verifies,
and packages exactly as a tag would, tolerates an unfinalized changelog, and
uploads a `release-dry-run` artifact instead of creating a release. Do this
before tagging.
