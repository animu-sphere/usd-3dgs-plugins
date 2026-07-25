# Current

The active development target is **v0.5.0 — SOG v2 one-object import**, defined
in the [release plan](release-plan.md#v050--sog-v2-one-object-import): import one
complete PlayCanvas SOG v2 object into the same standard USD representation PLY
and SPZ produce, exercising the v0.4.0 contract with a third format.

This is the first release that *uses* the Gaussian Import Foundation rather than
building it. Nothing in `libs/` changed to accommodate SOG: the bundle targets
the published `GaussianCloudData` contract, validates through the shared gate,
allocates through the shared overflow-checked helpers, converts frames with the
shared `FlipYZAxes`, and authors through the unchanged `GaussianLayerWriter`.
That the v0.4.0 seam needed no adjustment for a format as unlike PLY as a
codebook-and-image container is the release's real result.

v0.1.0 through v0.4.0 are tagged and published; their completed milestone detail
is recorded in the [delivery history](../reports/delivery-history.md) and the
[release records](../releases/README.md).

Legend: ✅ done · 🚧 in progress · ⬜ not started · ⛔ blocked

## Carried over from earlier stabilization

Release-engineering items that remain live across releases:

- ⬜ Investigate the macOS across-run package digest difference (suspected
  Mach-O `LC_UUID`/timestamp analog). Windows and Linux `tar.zst` archives are
  byte-identical across hosted runs since v0.2.0; see
  [releases/v0.2.0.md](../releases/v0.2.0.md) and
  [dogfooding report 02](../reports/ost/02-2026-07-19-package-provenance-and-reproducibility.md).
- 🚧 Make package-origin L5 execute rather than skip. OST 0.18.0 packaged the
  roundtrip PLY fixture but not its adjacent `.golden.usda`, and the bundle
  manifest has no golden declaration
  ([dogfooding report 01](../reports/ost/01-2026-07-18-v0.18.0-bootstrap.md)).
  **Resolved upstream between 0.18.0 and 0.20.0:** on OST 0.20.0,
  `ost plugin test <bundle> --from-package --up-to 5` executes and passes L5 for
  `gaussian-ply` and `gaussian-sog` (observed 2026-07-25). The CI pin moved to
  0.20.0 in the same change, so this closes once a hosted run confirms it on
  macOS and Linux — the local observation is Windows only.
- ⬜ Decide whether Windows remains capped at OST L4 or can run the same L5
  golden gate as macOS/Linux. Local Windows L5 passes; the cap is inherited
  from the reference workspace's hosted multiline-USDA line-ending finding.
- ⬜ Add a lightweight link/language check to CI so public Markdown remains
  English and local links resolve.

## 1. Format facts and dependency decisions ✅

*Goal: every SOG v2 constant quoted from the reference implementation, and the
container libraries vendored and pinned, before decoder code exists.*

- ✅ [SOG_MAPPING.md](../reference/SOG_MAPPING.md): the normative semantic
  mapping — split-precision inverse-log positions, the scale/DC/SH codebooks,
  smallest-three quaternions with the largest-component tag, the SH
  palette/label/centroid encoding, and the OpenUSD mapping — with every formula
  quoted from PlayCanvas SplatTransform (`write-sog.ts`, `read-sog.ts`,
  `math.ts`) rather than inferred.
- ✅ **Coordinate frame decided: SOG stores PLY-native (Graphdeco RDF)
  columns**, so the decoder applies the same shared `FlipYZAxes` the PLY decoder
  applies. This confirms the duty [ADR 0001](../adr/0001-model-frame-is-rub.md)
  anticipated for SOG. The developer documentation's contradictory y-up/z-back
  prose describes the PlayCanvas *engine* frame, not the on-disk columns; the
  discrepancy is resolved in favour of the implementation and pinned by the
  equivalence triples ([SOG_MAPPING.md §5](../reference/SOG_MAPPING.md)).
- ✅ libwebp v1.6.0 decoder subset vendored (BSD-3-Clause, exact commit
  recorded in `third_party/libwebp/VENDORING.md` and
  [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)); ZIP reading reuses
  the already-vendored miniz 3.0.2 with its archive API enabled for this bundle
  only. No new dependency beyond the WebP decoder.

## 2. Container reader ✅

*Goal: one reader owning every SOG container concern, with diagnostics granular
enough to tell a user which part of their file is wrong.*

- ✅ `plugins/gaussian-sog/src/io/SogReader.*`: layout detection by content, ZIP
  central-directory walking, `meta.json` schema and version validation, codebook
  and range checks, lossless-WebP plane decoding, and plane presence/dimension
  checks. A lossy plane is rejected rather than decoded approximately, because
  it would silently corrupt positions.
- ✅ `meta.json` is parsed by a strict in-repo reader (`src/io/SogJson.*`)
  rather than a vendored JSON library, per
  [SOG_FORMAT.md §2](../reference/SOG_FORMAT.md): every malformed shape has to
  surface as a specific `GSSOG` code. It rejects comments, trailing commas,
  duplicate keys, and `NaN`/`Infinity`, bounds nesting depth, and reads numbers
  under the classic locale so a host with a decimal comma cannot change how
  positions decode.
- ✅ The unbundled layout's companion planes load through an injected loader;
  the plugin supplies an `ArResolver`-backed one, which keeps the reader
  USD-free and unit-testable while a SOG whose planes sit behind a custom
  resolver still resolves them the way USD resolves any companion asset.
- ✅ Hardening: plane names are confined to bare file names, so a hostile
  `meta.json` cannot reach outside its own directory; every declared size is
  bounded before allocation; and routing reads a bounded prefix only.

## 3. Semantic decoder ✅

*Goal: SOG's stored form becomes the shared model, with no format-specific USD
authoring anywhere.*

- ✅ `plugins/gaussian-sog/src/io/GaussianSogDecoder.*`: split-precision
  inverse-log positions, exponential scale codebooks, smallest-three quaternion
  unpacking through the shared `NormalizeQuaternion`, raw-DC and opacity
  decoding, palette-resolved higher-order SH, then the shared `FlipYZAxes` into
  the model's RUB frame and the shared validation gate. Allocation goes through
  `GaussianSizeMath.h`.
- ✅ SOG `bands` 1-3 are exactly SH degrees 1-3 and no `shN` is degree 0, so the
  whole format range sits inside the model's supported degrees: unlike SPZ, SOG
  needs no degree-ceiling rejection.
- ✅ The import-statistics seam is filled and emitted under
  `TF_DEBUG=GSSOG_IMPORT_STATS`, and `Read(metadataOnly=true)` authors the
  contract from `meta.json` alone with no plane decoded.

## 4. Diagnostics ✅

- ✅ `GSSOG-E002`-`E015` cover the container and semantic failures with the
  malformed / unsupported / internal distinction intact — a legacy SOG v1 file
  is told its version is unsupported, not that it is corrupt — plus `GSSOG-W001`
  for palette labels past the palette. The shipped catalog
  (`plugin/resources/gaussian-sog/diagnostics.json`) and the source constants
  are cross-checked in both directions.
- ✅ `GSSOG-E001` (the v0.4.0 skeleton's not-implemented code) is retired from
  the header and the catalog, and never reused.

## 5. Routing ✅

- ✅ Bundled `.sog` is claimed by extension plus the ZIP signature. The
  unbundled layout means registering the far broader `.json` extension, so its
  gate is strict: `version == 2` plus the four required SOG property
  descriptions with their `files` arrays. Unrelated JSON is declined, while a
  defective SOG `meta.json` past the gate still reaches `Read()` for a specific
  diagnostic instead of a silent routing refusal
  ([SOG_FORMAT.md §6](../reference/SOG_FORMAT.md), maintainer-ratified).

## 6. Fixtures, tests, and equivalence ✅

- ✅ `tools/generate_fixtures.py` writes every SOG fixture from source with no
  third-party dependency: it contains a minimal lossless-WebP (VP8L) writer, so
  no WebP *encoder* is vendored merely to build test data, and applies the
  reference encoder's own quantization equations. Fixtures are byte-reproducible
  on every platform.
- ✅ Decoder-test-kit round trip: the positive fixtures encode the kit's
  canonical clouds, and the decoder test requires `CompareClouds` to be empty at
  tolerances derived from the documented quantization steps — which pins Gaussian
  order, coefficient order, channel order, the quaternion convention, the frame,
  and the derived extent at once.
- ✅ One negative fixture per container diagnostic, both layouts covered, and the
  Python smoke test asserting the same stage contract PLY and SPZ assert plus the
  routing gates and the read-only entry point.
- ✅ Cross-format equivalence is now PLY/SPZ/SOG triples
  ([EQUIVALENCE.md](../reference/EQUIVALENCE.md)). PLY and SOG each apply the
  frame conversion while SPZ applies none, so agreement pins the frame and the
  15-entry SH sign table from both directions. SOG's position bound is derived
  relatively — half a log-domain code step amplified by `|p|+1` — and its
  codebooks are exact, which keeps the SH comparison at 1e-6.
- ✅ `ost plugin test plugins/gaussian-sog` is green through L5, including the
  golden roundtrip, and `--from-package` is green through L5 as well (on the
  locally installed OST 0.20.0; see the carried-over note above).

## 7. Build, package, and release onboarding ✅

- ✅ The bundle builds standalone through `ost` and in the plain root
  composition; the equivalence directory grew a second executable because two
  bundles cannot share one vendored `miniz` translation unit (recorded in
  [EQUIVALENCE.md §6](../reference/EQUIVALENCE.md)).
- ✅ The CI cells moved to the shipping shape (Windows L4, macOS/Linux L5) and
  the skeleton's `publish: never` marker is gone, which is the *entire* release
  onboarding for the bundle: `scripts/release.py` derives three
  `gaussian-sog-release-*` cells from the same source cells with no copied
  logic. This is the v0.4.0 declarative-scaling claim paying out.

## 8. Documentation synchronization 🚧

- ✅ Bundle README rewritten from skeleton notice to importer documentation;
  root README lists SOG among the formats it reads, with its own container
  table.
- ✅ [SOG_MAPPING.md](../reference/SOG_MAPPING.md) corrected against the
  implementation in two places: SOG needs **no** PLY-style `f_rest` transpose
  (each centroid texel already interleaves one coefficient's three channels),
  and `count == 0` is well-formed SOG that is rejected at import rather than
  authored as an empty stage.
- ✅ This file reframed from the v0.4.0 breakdown to the v0.5.0 workstreams;
  release plan sequence table updated.
- 🚧 Capability matrix, supported configurations, and the documentation index
  extended to a third format.
- ⬜ Release record for v0.5.0.

## 9. Real-asset validation 🚧

*Goal: the design-policy §17 real-asset gate and §12.1 performance baselines,
which synthetic fixtures cannot substitute for.*

- ✅ Two provenance-recorded real SOG assets (`yashica-t4` and `leica-sofort`)
  are committed under `plugins/gaussian-sog/tests/corpus/`. The Python smoke
  test discovers them and validates their decoded stages semantically. They
  are format-conversion corpus inputs, not cross-format equivalence fixtures;
  the latter remain the synthetic PLY/SPZ/SOG triples.
- ⬜ §12.1 performance baselines still need a dedicated measurement pass over
  the committed SOG corpus; the corpus admission itself is complete.

## Completion criteria

v0.5.0 is complete when:

1. Both SOG layouts open through the plugin on the same semantic path. ✅
2. Position, scale, rotation, opacity, SH0, and optional higher-order SH decode
   correctly, verified against known values and the decoder test kit. ✅
3. Cross-format fixtures demonstrate coordinate and SH consistency with PLY and
   SPZ at tolerances derived from the SOG equations. ✅
4. Failures use stable, actionable `GSSOG-****` diagnostics with a shipped
   catalog. ✅
5. A provenance-recorded real SOG asset is validated automatically and manually,
   with §12.1 performance baselines recorded. ⛔
6. Windows, macOS, and Linux source and package cells pass. 🚧 (local Windows
   green through L5; hosted cells run on the pull request)
