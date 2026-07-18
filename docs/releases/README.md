# Release records

Each tagged version receives an immutable record here: objective, shipped
capabilities, compatibility, artifact names and digests, and known limitations.
Release records are history and are not rewritten after publication.

No version has been tagged or published yet. The current target is v0.1.0; its
incomplete release work is tracked in [roadmap/current.md](../roadmap/current.md).
Do not create `v0.1.0.md` until the tag exists.

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

