# SOG format scope and decisions

This records the specification source, licence review, dependency decisions,
and the implementation and fixture plan the v0.4.0
[release plan](../roadmap/release-plan.md#v040--gaussian-import-foundation)
requires *before* SOG decoding is implemented (workstream 9 in
[current.md](../roadmap/current.md)). It is the SOG counterpart of
[SPZ_FORMAT.md](SPZ_FORMAT.md). The format-independent model the decoder
targets is [GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md); the
SOG-specific semantic mapping is [SOG_MAPPING.md](SOG_MAPPING.md), mirroring
[SPZ_MAPPING.md](SPZ_MAPPING.md).

Status: plan accepted 2026-07-22 with the v0.4.0 `gaussian-sog` bundle
skeleton (`plugins/gaussian-sog/`, no decoding). The v2 container facts were
confirmed against the reference implementation and the §6 decisions
(coordinate frame, `CanRead()` bounds, libwebp pin) recorded 2026-07-22 for
the v0.5.0 cycle; the full semantic mapping is now
[SOG_MAPPING.md](SOG_MAPPING.md). The remaining §6 item — a provenance-recorded
real SOG asset — stays open. Production decoding follows the confirmed facts
below.

## 1. Specification source

SOG ("Splat Object Graphics") is published by PlayCanvas with a versioned
specification and an open-source reference toolchain:

- the format description in the PlayCanvas developer documentation;
- [SplatTransform](https://github.com/playcanvas/splat-transform) (MIT), the
  reference encoder/converter, which doubles as the deterministic test-asset
  generator;
- the SuperSplat editor (MIT) as an active producer and consumer.

The binary layout is therefore **documented, not inferred**, satisfying the
standing rule that undocumented layouts are never guessed
([design policy](../design/DESIGN_POLICY.md) §4.4). The spec is the normative
source; where the reference implementation and the prose disagree, the
discrepancy is recorded here and resolved deliberately.

The v2 facts in §4 and the equations in [SOG_MAPPING.md](SOG_MAPPING.md) were
confirmed 2026-07-22 against SplatTransform at `main` — the encoder
`src/lib/writers/write-sog.ts`, the decoder `src/lib/readers/read-sog.ts`, and
the coordinate convention `src/lib/utils/math.ts` (`Transform.PLY`) — quoted,
not inferred. One prose-vs-implementation disagreement surfaced and was
resolved in favour of the implementation: the developer-documentation page
describes a y-up/z-back frame, while the implementation stores PLY-native
(Graphdeco) coordinates (§4, [SOG_MAPPING.md §5](SOG_MAPPING.md)).

Target: **SOG v2** (the `meta.json` `version: 2` generation), both layouts:

- **bundled** — one `.sog` file, a ZIP archive holding `meta.json` plus
  lossless-WebP property images;
- **unbundled** — `meta.json` opened directly, with the property images as
  resolver-backed companion files.

Streamed SOG (`lod-meta.json`, spatial chunks, LOD) is SOG M2-M4 and excluded
from v0.5.0.

## 2. Decision — implement from the specification

**Accepted: implement the container reader (ZIP + meta.json + image-plane
indexing) and all dequantization in this repository; use libraries only for
ZIP inflation and lossless-WebP bitstream decoding.**

This is the same split PLY and SPZ use (tinyply / miniz vendored for container
mechanics, every Gaussian-specific decision in-repo), and for the same reason:
the `GSSOG-****` malformed / unsupported / internal diagnostic distinctions
are a release criterion, and wholesale-vendored decoders cannot express them.

## 3. Dependency decisions and licence review

| Concern | Decision | Licence | Pin |
| --- | --- | --- | --- |
| ZIP reading (bundled `.sog`) | Reuse the already-vendored **miniz 3.0.2** (`third_party/miniz/`), enabling its archive-reading API for this bundle only — `gaussian-spz` keeps compiling it with `MINIZ_NO_ARCHIVE_APIS`. No new dependency. | MIT (already reviewed for v0.3.0, [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)) | already vendored at 3.0.2 |
| Lossless WebP decoding | Vendor the **decoder subset** of Google's **libwebp** into `third_party/libwebp/` (upstream's `libwebpdecoder` composition — `dec/` + decode-path `dsp/`/`utils/`); the encoder, mux, demux, and `sharpyuv` trees are dropped, mirroring how miniz is trimmed by excluded APIs. The decode driver is kept intact (VP8 + VP8L); lossy input is rejected at the reader, not removed from the library. | BSD-3-Clause + PATENTS | **v1.6.0**, commit `4fa2191233…` (`third_party/libwebp/VENDORING.md`, THIRD_PARTY_NOTICES.md) |

Rejected alternatives: a system/package-manager WebP (breaks the hermetic,
digest-reproducible package gate), and image libraries larger than the need
(stb-style single-header decoders do not cover lossless WebP; full imaging
stacks pull formats this plugin must never claim).

SOG property images are **lossless** WebP by specification; a lossy plane
would silently corrupt positions. The reader therefore rejects a lossy VP8
bitstream as malformed rather than decoding it approximately.

## 4. Implementation plan (v0.5.0)

One reader, one decoder, one writer — the v0.4.0 contract exercised by a
third format:

- `plugins/gaussian-sog/src/io/SogReader.*` — layout detection (ZIP signature
  vs `meta.json`), ZIP central-directory walking, `meta.json` schema/version
  validation, image-plane presence and dimension checks, and resolver-backed
  companion loading for the unbundled layout. Container diagnostics stay
  here.
- `plugins/gaussian-sog/src/io/GaussianSogDecoder.*` — semantic decoding into
  `GaussianCloudData`: `means_l`/`means_u` split-precision positions with the
  documented inverse-log remapping, exponential scale codebook lookup,
  smallest-three quaternion unpacking, SH0/base-colour and opacity decoding,
  and optional higher-order SH palette/label resolution. All allocation
  through `GaussianSizeMath.h`; quaternions through `NormalizeQuaternion`;
  the SOG source frame converted to the model's RUB frame per
  [ADR 0001](../adr/0001-model-frame-is-rub.md) (the exact conversion is a
  §6 open item, resolved against real assets before decoding lands).
- USD authoring through the unchanged shared `GaussianLayerWriter`;
  format-specific USD construction remains forbidden.
- Diagnostics: the `GSSOG-****` namespace already shipped with the skeleton
  (`GSSOG-E001` not-implemented). The catalog follows the allocation plan in
  `GaussianSogDiagnostics.h` (E0xx container/semantic, E1xx internal/USD,
  E2xx entry point, W0xx warnings); codes are never renumbered or reused.

## 5. Fixture and equivalence plan

- **Deterministic fixtures** — hand-built minimal `meta.json` + tiny property
  images with hand-computed expected values (the `TestShLayout` discipline),
  for both layouts; malformed cases for every container diagnostic
  (bad ZIP, missing plane, dimension mismatch, unsupported `version`, lossy
  plane, truncation).
- **Decoder test kit** — the SOG decoder encodes the kit's canonical clouds
  (`openstrata/gs/testing/DecoderTestKit.h`) into SOG and must decode them
  back to `CompareClouds`-empty, with tolerances derived from the SOG
  quantization equations, before any real asset is attempted.
- **Cross-format equivalence** — `tools/generate_equivalence_fixtures.py`
  grows a SOG encoder so the existing one-source-model PLY/SPZ pairs become
  PLY/SPZ/SOG triples in `tests/equivalence/`.
- **Real assets** — at least one provenance-recorded real SOG asset (own
  capture converted with a pinned SplatTransform release, provenance and
  checksums recorded as for the SPZ corpus, design policy §17), validated
  automatically and manually, with §12.1 performance baselines recorded.

## 6. Open items

Resolved 2026-07-22 against the reference implementation (§1); the equations
they settle are in [SOG_MAPPING.md](SOG_MAPPING.md).

- **Container facts — confirmed.** Plane names and dtypes, the codebook sizes
  (256 entries for `scales`/`sh0`/`shN`), the split-precision `means`
  inverse-log equation, the smallest-three quaternion packing and
  largest-component tag, and the `shN` palette/label/centroid encoding are
  quoted from `write-sog.ts`/`read-sog.ts` and recorded in
  [SOG_MAPPING.md](SOG_MAPPING.md) §2-§7. No fact required correction from the
  proposal; the one disagreement was the coordinate frame (below).

- **Coordinate frame — decided: PLY-native (Graphdeco RDF), converted with the
  shared `FlipYZAxes`,** exactly as the PLY decoder does. The reference writer
  bakes to `Transform.PLY` and the reader reports `Transform.PLY` with no
  per-component negation, and `math.ts` documents `Transform.PLY` as the
  convention shared by "PLY, splat, KSplat, SPZ, and SOG" — so a SOG file
  stores the same raw columns a Graphdeco `.ply` stores. This confirms the
  duty [ADR 0001](../adr/0001-model-frame-is-rub.md) anticipated for SOG and
  deferred to its mapping document; the derivation and the resolution of the
  contradictory y-up/z-back prose are in [SOG_MAPPING.md §5](SOG_MAPPING.md).

- **`CanRead()` bounds — decided,** mirroring the SPZ §6 bounded-routing rule:
  - *bundled* `.sog` is claimed by the `.sog` extension plus the ZIP local-file
    signature (`PK\x03\x04`) at offset 0; the presence and contents of the
    archived `meta.json` are validated in `Read()`, not during routing.
  - *unbundled* `meta.json` is routed by registering the `.json` extension and
    claiming **only** when a bounded prefix parses as a JSON object with
    `version == 2` and the required SOG property keys (`means`, `scales`,
    `quats`, `sh0`) each carrying their `files` array. The strict shape keeps
    the broad `.json` registration from claiming unrelated JSON; a defective
    SOG `meta.json` past that gate still reaches `Read()` for a specific
    diagnostic rather than a silent routing refusal. The broad `.json`
    registration is the deliberate, maintainer-ratified (2026-07-22)
    consequence of supporting the stock unbundled layout; the strict
    `version == 2` + SOG-keys gate is what keeps it from claiming unrelated
    JSON.

- **libwebp pin — decided: v1.6.0** (§3), the current upstream stable. The
  exact commit and the BSD-3-Clause notice land with the vendored source,
  before any decoder code, in the vendoring workstream.

Still open:

- **A provenance-recorded real SOG asset** for the corpus (own capture
  converted with a pinned SplatTransform release, provenance and checksums
  recorded as for the SPZ corpus, design policy §17), validated automatically
  and manually with §12.1 performance baselines. Deterministic fixtures and the
  decoder-test-kit round-trip (§5) do not need it; the real-asset validation
  gate does.
