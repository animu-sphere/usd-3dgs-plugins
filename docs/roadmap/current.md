# Current

The active milestone is the first tagged release. M0-M4 implementation detail is
already complete and recorded in the
[delivery history](../reports/delivery-history.md).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## v0.1 release stabilization 🚧

*Goal: turn the locally green Gaussian PLY vertical slice into an observed,
portable, documented release across the declared support matrix.*

### Hosted support evidence

- ⬜ Run the generated Windows 2022, macOS 15 arm64, and Ubuntu 24.04 PR cells.
- ⬜ Record compiler/runtime/package digests from those runs in the first
  release record.
- ⬜ Decide whether Windows remains capped at OST L4 or can run the same L5
  golden gate as macOS/Linux. Local Windows L5 passes; the cap is inherited from
  the reference workspace's hosted multiline-USDA line-ending finding.

Done when: every row in
[SUPPORTED_CONFIGURATIONS.md](../reference/SUPPORTED_CONFIGURATIONS.md) is
observed rather than merely declared.

### Packaged-consumer closure

- ⛔ Make package-origin L5 execute rather than skip. OST 0.18.0 packages the
  roundtrip PLY fixture but not its adjacent `.golden.usda`, and the bundle
  manifest has no golden declaration. This is an upstream packaging/test seam;
  see [dogfooding report 01](../reports/ost/01-2026-07-18-v0.18.0-bootstrap.md).
- ⬜ Exercise the extracted package from a clean directory outside the worktree.
- ⬜ Verify manual OpenUSD activation on Windows in addition to the supported
  `ost plugin run <package>` path.

Done when: an extracted package opens the binary and ASCII fixtures without a
build-tree path, and every intended package-origin verification level has a
real gate or a documented versioned exception.

### Release hygiene

- 🚧 Add a tag-driven release workflow. Implemented as
  [release.yml](../../.github/workflows/release.yml), driven by
  [scripts/release.py](../../scripts/release.py), which derives its matrix from
  `openstrata.ci.yaml` so the release and PR lanes cannot pin different
  runtimes. Not yet observed on hosted runners; exercise it with a
  `workflow_dispatch` dry run before tagging.
- 🚧 Prove package digest reproducibility on each target. The release lane
  packages twice and fails on disagreeing digests; reproducibility is verified
  locally on Windows only, so the macOS and Linux cells remain unobserved.
- ⬜ Finalize the `CHANGELOG.md` v0.1.0 section and create
  `docs/releases/v0.1.0.md` only when the tag exists.
- ⬜ Publish a draft release for human review; publishing remains a human action.

### Real-asset confidence

- ⬜ Select at least one redistributable, provenance-recorded Gaussian PLY with
  non-trivial SH degree and more than one Gaussian.
- ⬜ Record peak memory, decode time, USD authoring time, and output array sizes.
- ⬜ Add it as an opt-in corpus/performance test unless its size is appropriate
  for normal CI.

No external trained asset enters the repository before license and provenance
review.

## Documentation consistency 🚧

- ⬜ Add a lightweight link/language check to CI so public Markdown remains
  English and local links resolve.
- ⬜ Add stable diagnostic codes before external tools begin depending on the
  current free-text messages; this is desirable for v0.1 but not required for
  the semantic vertical slice.

