# Release records

Each tagged version receives an immutable record here: objective, shipped
capabilities, compatibility, artifact names and digests, and known limitations.
Release records are history and are not rewritten after publication.

| Version | Date | Record |
| --- | --- | --- |
| v0.2.0 | 2026-07-19 | [v0.2.0.md](v0.2.0.md) — production-ready Graphdeco PLY import; published 2026-07-19 |
| v0.1.0 | 2026-07-19 | [v0.1.0.md](v0.1.0.md) — first tagged release; published 2026-07-19 |

Create a record only once its tag exists: it pins the tagged commit, the
consumed runtime digests, and the published artifact checksums, none of which
are known before the release lane runs. Remaining work is tracked in
[roadmap/current.md](../roadmap/current.md).

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
[release.yml](../../.github/workflows/release.yml), which enforces items 1, 2,
4, and 6 automatically:

| Gate item | Enforced by |
| --- | --- |
| 1. version agreement | `scripts/release.py guard` — tag, `VERSION`, every bundle manifest, and a finalized `## [X.Y.Z]` changelog heading |
| 2. hosted cells pass | one job per source cell in `openstrata.ci.yaml`, at that cell's `up_to` level |
| 4. reproducible digests | the lane packages twice and fails on disagreeing `archive_digest` |
| 6. draft for review | `gh release create --draft`; publishing stays a human action |

Item 3's package-origin half and item 5 are not yet machine-enforced; item 3 is
blocked upstream (see [roadmap/current.md](../roadmap/current.md)).

The workflow takes its runtime digests, `ost` version, and per-cell levels from
`openstrata.ci.yaml` rather than restating them, so re-pinning a runtime moves
the PR and release lanes together.

Run the lane manually (`workflow_dispatch`) for a dry run: it builds, verifies,
and packages exactly as a tag would, tolerates an unfinalized changelog, and
uploads a `release-dry-run` artifact instead of creating a release. Do this
before tagging.

