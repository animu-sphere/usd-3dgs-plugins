# Current

The active milestone is closing out the first tagged release; the next
development target is **v0.2.0 — production-ready Graphdeco PLY import**,
defined in the [release plan](release-plan.md). M0-M4 implementation detail is
already complete and recorded in the
[delivery history](../reports/delivery-history.md).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

## v0.1 release stabilization 🚧

*Goal: turn the locally green Gaussian PLY vertical slice into an observed,
portable, documented release across the declared support matrix.*

### Hosted support evidence

- ✅ Run the generated Windows 2022, macOS 15 arm64, and Ubuntu 24.04 PR cells.
  All three pass; first observed on PR #1.
- ✅ Record compiler/runtime/package digests from those runs in the first
  release record. See [releases/v0.1.0.md](../releases/v0.1.0.md).
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
- ✅ Exercise the extracted package from a clean directory outside the worktree.
  Observed 2026-07-19 on Windows: manifest-verified extraction, then
  `ost plugin run <extracted-root>` opened the ASCII, binary, and degree-1 SH
  fixtures with the plugin DLL resolved from the extracted tree. See
  [dogfooding report 02](../reports/ost/02-2026-07-19-package-provenance-and-reproducibility.md).
- ✅ Verify manual OpenUSD activation on Windows in addition to the supported
  `ost plugin run <package>` path. Observed 2026-07-19 for both a `usdcat` host
  and a plain Python host; the working requirements are recorded in
  [INSTALL.md](../guides/INSTALL.md#manual-package-activation).

Done when: an extracted package opens the binary and ASCII fixtures without a
build-tree path, and every intended package-origin verification level has a
real gate or a documented versioned exception.

### Release hygiene

- ✅ Add a tag-driven release workflow.
  [release.yml](../../.github/workflows/release.yml), driven by
  [scripts/release.py](../../scripts/release.py), derives its matrix from
  `openstrata.ci.yaml` so the release and PR lanes cannot pin different
  runtimes. Exercised by two dry runs, then by the v0.1.0 tag.
- 🚧 Prove package digest reproducibility on each target. All three cells pass
  the *within-run* gate: each packages the same build twice and fails on
  disagreeing digests. The Windows across-run cause is now identified and fixed:
  MSVC embedded wall-clock timestamps in objects, archive members, the PE
  header, and the debug directory; `/Brepro` removes them, and two fully clean
  local build+package cycles now hash identically. Remaining: observe the fixed
  behavior across two hosted Windows runs, and investigate the macOS across-run
  difference (suspected Mach-O `LC_UUID`/timestamp analog). See
  [dogfooding report 02](../reports/ost/02-2026-07-19-package-provenance-and-reproducibility.md).
- ✅ Finalize the `CHANGELOG.md` v0.1.0 section and create
  `docs/releases/v0.1.0.md` only when the tag exists.
- 🚧 Publish a draft release for human review; publishing remains a human
  action. The v0.1.0 draft is assembled and awaiting that review.

### Real-asset confidence

- ✅ Select at least one redistributable, provenance-recorded Gaussian PLY with
  non-trivial SH degree and more than one Gaussian. Done 2026-07-19:
  [tests/corpus/yashica-t4](../../plugins/gaussian-ply/tests/corpus/yashica-t4/PROVENANCE.md)
  — an author-captured scene trained with Brush v0.3.0 (Apache-2.0), dedicated
  CC0-1.0; SH degree 3, 8,192 Gaussians, derived deterministically by
  `scripts/ply_subset.py` and covered by tolerance-based semantic tests.
  Candidate large references remain Mip-NeRF 360 `garden` and `bicycle`,
  pending the same review, and stay download-on-demand data in any case.
- ⬜ Record the design-policy baseline metrics (§12.1): source size, Gaussian
  count, `CanRead()` and full `Read()` durations, peak resident memory,
  temporary USDA and generated USDC sizes, flattening duration, and time until
  the stage is inspectable in `usdview`.
- ⬜ Add it as an opt-in corpus/performance test unless its size is appropriate
  for normal CI.

No external trained asset enters the repository before license and provenance
review.

## Next: v0.2.0 — production-ready Graphdeco PLY import ⬜

*Goal: complete and stabilize Graphdeco PLY support before adding another
format. Scope and completion criteria are in the
[release plan](release-plan.md).*

Work begins once the v0.1 carry-over above closes, drawing on the
[PLY compatibility](backlog.md#ply-compatibility) and
[performance](backlog.md#performance-and-loading) backlogs in priority-ladder
order: real-dataset baselines (P0, tracked above under real-asset confidence),
metadata-only reads (P1), documented dialect compatibility with degree-2/3 SH
and additional malformed fixtures (P2), stable diagnostic identifiers, and the
first file-format arguments (`shDegree`, `opacityThreshold`,
`scaleMultiplier`), each gated on clear behavior and automated tests.

## Documentation consistency 🚧

- ⬜ Add a lightweight link/language check to CI so public Markdown remains
  English and local links resolve.
- ⬜ Add stable diagnostic codes before external tools begin depending on the
  current free-text messages; this is desirable for v0.1 but not required for
  the semantic vertical slice.

