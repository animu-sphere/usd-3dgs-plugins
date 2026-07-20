# OST dogfooding report #3 — L5 golden portability, and a misdiagnosis of it

- Date: 2026-07-20
- OST: 0.18.0
- Host: Windows x86_64, MSVC 14.34.31933 / toolset 143, Ninja
- Runtime: `openstrata-cy2026-windows-x86_64-py313-usd`, OpenUSD 26.05,
  CPython 3.13.14
- Context: v0.3.0 release hardening — adding three `gaussian-spz` source cells
  to `openstrata.ci.yaml` and giving the new bundle the same L5 golden
  roundtrip gate `gaussian-ply` already had.

## TL;DR

**There is no upstream defect here.** `ost` normalizes the generated-from-root-layer
`doc` block when comparing an L5 golden, so a golden containing one machine's
absolute path matches on every runner. Goldens are portable.

This report exists because the opposite was concluded first, on local evidence
that looked conclusive and was not. The failure mode is worth recording:
generating a golden with `--skipSourceFileComment` produces a file that fails
the comparison, and the resulting error is easy to misread as evidence that
the *path* is the problem rather than the *missing block*.

## What was observed

`ost plugin test plugins/gaussian-spz --up-to 5` reported L5 as skipped, with
no golden file. The suggested command was followed, but with
`--skipSourceFileComment` added — the flag this repository's own README uses
for `usdcat` conversions, chosen deliberately to keep a machine-specific path
out of a committed file. The resulting golden begins:

```
#usda 1.0
(
    defaultPrim = "Asset"
    metersPerUnit = 1
```

L5 then failed:

```
[FAIL] L5 golden.roundtrip  flattened output differs from the golden;
       first difference at line 4: expected "    metersPerUnit = 1",
       actual "    doc = \"\"\"Generated from Composed Stage of root layer "
```

Inspecting the committed `one-gaussian-ascii.ply.golden.usda` showed it *does*
carry the block, with a `C:\dev\...` path in it.

## The wrong conclusion

From those two facts the inference drawn was: `ost` flattens without
`--skipSourceFileComment`, therefore a golden must embed the generating
machine's absolute path, therefore a committed golden cannot match a hosted
runner whose checkout lives at `/home/runner/work/...`, therefore L5 is not a
real gate on any runner and `gaussian-spz` must keep `roundtrip: []` rather
than ship a cell that fails on every run.

Every step of that is a reasonable reading of the local evidence. The
conclusion is still wrong, because the premise it rests on — that the
comparison is literal — was never tested. The observed failure was equally
consistent with the block being *absent from the golden*, which is what had
actually happened.

## What settled it

The hosted CI logs for the run that added the SPZ cells:

```
gaussian-ply-pr-macos-arm64  "observed": "flattened output matches the golden"
gaussian-ply-pr-linux        "observed": "flattened output matches the golden"
```

The PLY golden, with its Windows `C:\dev\...` path, matches on macOS and on
Linux. So `ost` normalizes the block rather than comparing it literally.

Regenerating the SPZ golden with the plain command `ost plugin test` prints —
no `--skipSourceFileComment` — and declaring the roundtrip fixture:

```
[PASS] L5 golden.roundtrip   flattened output matches the golden
Result: OK (12 pass, 0 fail, 3 skip)
```

## Guidance

- Generate a golden with exactly the command `ost plugin test` prints. Do not
  add `--skipSourceFileComment`: the block it removes is normalized during
  comparison, and omitting it shifts every subsequent line.
- The absolute path inside a committed golden is inert. It is not a
  portability hazard, and it is not worth trying to strip. It does record
  which machine generated the file, which is a mild provenance leak in a
  public repository but not a correctness one.
- A local L5 failure whose "first difference" is at the top of the file is
  more likely a structural mismatch in the golden than a value disagreement.

## Suggestions for upstream

Neither of these is a defect; both would have prevented the misdiagnosis.

1. **Say that the block is normalized.** The skip message tells you how to
   generate a golden but not what the comparison does with the result. One
   clause — "the generated-from-root-layer doc block is normalized, so the
   golden is portable" — closes the gap.
2. **Detect the `--skipSourceFileComment` shape.** When the golden lacks the
   block and the flattened output has it, the diff is reported as a line-4
   value mismatch. Naming the likely cause ("the golden appears to have been
   generated with `--skipSourceFileComment`") would turn a confusing failure
   into an actionable one.

## Roadmap effect

No new roadmap item. The Windows L4 cap and the package-origin L5 skip are
unrelated and stand as previously recorded; this report removes a third item
that was briefly added between them and should not have been.

`gaussian-spz` now declares its roundtrip fixture and runs L5 on the macOS and
Linux cells, matching `gaussian-ply`.
