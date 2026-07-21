# ADR 0001 — The canonical model frame is RUB (Y-up)

- **Status:** Accepted (2026-07-22, maintainer-ratified)
- **Scope:** the reference frame of `GaussianCloudData`, every decoder's
  conversion duty, and the frame of all authored stage data
- **Supersedes:** the v0.1.0–v0.3.0 behavior in which the model frame was
  PLY-native RDF; see *Migration* below

## Context

`GaussianCloudData` needs exactly one reference frame, and every decoder must
convert into it (model contract §2; frame *inference* remains a non-goal,
design policy §20). Through v0.3.0 that frame was the Graphdeco-PLY-native
**right-down-front** (RDF, Y-down) — the frame v0.1.0 happened to author
first — while the writer simultaneously declared `upAxis = "Y"` on every
stage. The data therefore rendered upside down in any consumer that honors
USD stage metadata, and each consumer needed its own compensating flip.

SPZ made the cost concrete: its documented convention is **right-up-back**
(RUB, Y-up), so the v0.3.0 SPZ decoder converted RUB→RDF only for the stage
to contradict its own `upAxis` claim. SOG — the v0.5.0 format — stores
Graphdeco-convention data and would have inherited the same contradiction.
The project is pre-1.0; the release plan names v0.4.0 as the point where a
necessary authored-USD correction happens rather than waiting for a major
version.

## Decision

1. **The canonical frame of `GaussianCloudData` is RUB**: `+X` right, `+Y`
   up, `+Z` toward the viewer (backward), right-handed. This is exactly the
   convention of a USD stage with `upAxis = "Y"`, so the authored data and
   the authored stage metadata now agree. The authored `upAxis = "Y"` and
   `metersPerUnit = 1` remain importer defaults, not source-derived claims,
   and no compensating `xformOp` is authored — the data itself is upright.
2. **A decoder whose format's documented convention is a different frame
   applies the fixed constant conversion during decoding** — never the
   writer, never the consumer:
   - Graphdeco PLY (RDF): the decoder applies the shared `FlipYZAxes`
     helper (`gaussianCore`, `GaussianMath.h`) after decoding.
   - SPZ (RUB): no conversion — the container frame is the model frame.
   - SOG (v0.5.0, Graphdeco-convention data): expected to apply the same
     shared helper; the decision is confirmed in its mapping document when
     the decoder lands.
3. **The conversion is the Y/Z axis negation**, applied to positions, to
   quaternion vector parts, and to rest SH coefficients via a fixed sign
   table. It is an involution (RDF↔RUB both directions) and a proper
   rotation (180° about `+X`, determinant +1) — handedness is preserved and
   nothing is mirrored. Scales (per-axis standard deviations along the
   Gaussian's own principal axes), opacity, and the DC term (band 0,
   rotation-invariant) are unchanged.

## Derivation of the conversion

Negating two axes is the proper rotation `R = diag(1, -1, -1)`:

- **Positions** map as `(x, y, z) → (x, -y, -z)`.
- **Quaternions**: conjugating by `R` (itself the rotation quaternion
  `(0, 1, 0, 0)` up to sign) reduces, for this axis pair, to negating the
  matching vector components: `(w, x, y, z) → (w, x, -y, -z)`. This is the
  reference implementation's `flipQ = {y·z, x·z, x·y}` evaluated at
  `(x, y, z) = (+1, -1, -1)`, which coincides with the position flip.
- **Spherical harmonics**: a real SH basis function changes sign iff it is
  odd in an odd number of the negated axes. Evaluating the reference
  `flipSh` basis `{y, z, x, xy, yz, zz, xz, xx-yy, …}` at
  `(x, y, z) = (+1, -1, -1)` gives the per-rest-coefficient table
  (bands 1-3):

  ```text
  band 1 (0-2):   -1, -1, +1
  band 2 (3-7):   -1, +1, +1, -1, +1
  band 3 (8-14):  -1, +1, -1, -1, +1, -1, +1
  ```

  Because Y/Z negation is a within-family conversion (no 90° axis
  reordering), the flip is a per-coefficient sign only; no SH band rotation
  is required. Band 0 (DC) is isotropic and unchanged.

The single implementation is `FlipYZAxes` in `gaussianCore` with the table
`kShFlipYZ`, `static_assert`-tied to `kMaxShDegree`. It is verified against
independently restated tables in the `gaussianCore` and `gaussian-ply` unit
tests, and cross-checked end to end by the PLY/SPZ equivalence suite, where
the PLY decoder applies every sign and the SPZ decoder applies none.

## Alternatives considered

- **Keep RDF data and author a corrective `xformOp:rotateX = 180` on
  `/Asset`.** Composes correctly in USD, but every stage would carry a
  permanent 180° rotation, and any consumer reading the raw arrays (format
  converters, scene indexes) would still see Y-down data under a Y-up claim.
  Rejected in favor of correct canonical data.
- **Keep the mismatch and document it (status quo).** Defers the correction
  to a post-1.0 major version while every viewer grows a compensating flip.
  Rejected: pre-1.0 is exactly when this correction is cheap.

## Migration and consequences

- **Authored output changes.** Stages imported with v0.4.0 have Y and Z
  negated on positions, orientations (vector part), and non-DC SH
  coefficients relative to v0.1.0–v0.3.0 output, and extents move
  accordingly. Assets now render upright in `upAxis`-honoring consumers
  without delegate-side flips. Re-import is the migration path; no tool
  rewrites previously exported layers.
- **Cross-format equivalence is unchanged as a criterion** — two encodings
  of one object still decode to one model; the equivalence fixtures were
  regenerated with the generator's flip moved from the SPZ encoder to the
  PLY encoder.
- **Golden stages, decoder fixtures' expected values, and the corpus
  provenance bounds check** were updated in the same change; diagnostic
  codes and counts are untouched.
- **Performance** is unaffected at measurement resolution: PLY gains one
  linear in-place pass, SPZ loses per-element multiplications.
- The SPZ mapping's former §5 derivation moved here; `SPZ_MAPPING.md` and
  `PLY_MAPPING.md` now state each format's conversion duty and cite this
  ADR for the derivation.
