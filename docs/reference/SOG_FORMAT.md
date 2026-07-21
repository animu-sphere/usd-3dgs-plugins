# SOG format scope and decisions

This records the specification source, licence review, dependency decisions,
and the implementation and fixture plan the v0.4.0
[release plan](../roadmap/release-plan.md#v040--gaussian-import-foundation)
requires *before* SOG decoding is implemented (workstream 9 in
[current.md](../roadmap/current.md)). It is the SOG counterpart of
[SPZ_FORMAT.md](SPZ_FORMAT.md). The format-independent model the decoder
targets is [GAUSSIAN_MODEL_CONTRACT.md](GAUSSIAN_MODEL_CONTRACT.md); the
SOG-specific semantic mapping will live in `SOG_MAPPING.md`, mirroring
[SPZ_MAPPING.md](SPZ_MAPPING.md).

Status: plan accepted 2026-07-22 with the v0.4.0 `gaussian-sog` bundle
skeleton (`plugins/gaussian-sog/`, no decoding). Production decoding is the
v0.5.0 theme and must not begin before the open items in §6 are resolved in
this document.

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
| Lossless WebP decoding | Vendor the decoder subset of Google's **libwebp** into `third_party/libwebp/`, trimmed to lossless decode (no encoder, no lossy VP8, no I/O helpers), mirroring how miniz is trimmed by compile definitions. | BSD-3-Clause | exact upstream release tag and commit recorded in `third_party/libwebp/` and THIRD_PARTY_NOTICES.md **at vendoring time, before any decoder code lands** |

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

## 6. Open items (resolve before decoding lands)

- Pin the exact libwebp release tag/commit and land the licence text in
  [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) (§3).
- Confirm the container facts against the v2 specification and a real
  SplatTransform export — plane names and dtypes, codebook sizes, the
  `means` inverse-log equation, quaternion packing order, palette/label
  encoding for SH bands — and record them here as SPZ_FORMAT §4 does,
  including any corrections.
- Decide the SOG→RUB frame conversion from the documented SOG convention
  (SuperSplat/PlayCanvas world axes) and record it in the ADR-0001 frame
  table before fixtures are generated.
- Decide `CanRead()` bounds for the unbundled layout (how much of a
  `meta.json` is read during routing) mirroring the SPZ §6 bounded-routing
  decision.
