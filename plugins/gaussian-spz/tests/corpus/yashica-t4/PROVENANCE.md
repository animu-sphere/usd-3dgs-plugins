# yashica-t4 (SPZ)

A real Gaussian Splatting asset in SPZ form: a Yashica T4 compact film camera.
The [PLY corpus](../../../../gaussian-ply/tests/corpus/yashica-t4/PROVENANCE.md)
holds a same-subject asset from an independent capture and a different
pipeline; the two are **not** two encodings of one reconstruction and must not
be compared value-for-value.

## Capture and reconstruction

- Captured and reconstructed on-device with
  [Scaniverse](https://scaniverse.com/) (Niantic), which both scans and trains
  the splat; there is no separate trainer stage and no COLMAP step.
  _(Pending: app version, device model, capture date.)_
- The repository author owns the capture and the photographed object.
- Source export: `yashica_t4.spz`, 55,662 Gaussians, 1,185,360 bytes, SHA-256
  `46c8258dc96583dbe628fb5d24058b1828bb105c22ed456298b8685981facbff`
  (decompressed payload 3,562,384 bytes, SHA-256
  `81b121c81411ce4dfc67b8c3a6ed2d664fd5b118c8630ee33fb2b6bdb8c4b0c2`).
  The full export is retained outside the repository; only the subset below is
  committed.

## Committed subset

- `yashica-t4-top8192.spz`: the 8,192 most opaque Gaussians inside the
  axis-aligned box `[-0.25, 0.25]³` of the source export, original point order
  preserved, derived by
  `scripts/spz_subset.py --top-n 8192 --aabb=-0.25,-0.25,-0.25,0.25,0.25,0.25`.
  The crop runs before the opacity ranking and is required — see the dialect
  notes. 43,444 of the 55,662 source Gaussians survive it.
- Selection copies the retained per-point records **byte-for-byte** out of the
  quantized planes, so the subset dequantizes to exactly the values the
  corresponding source points do. Exact hashes and parameters are in
  [`yashica-t4-top8192.spz.provenance.json`](yashica-t4-top8192.spz.provenance.json).

## Dialect notes

Observations from this export, all of which the reader must tolerate:

- **Far-field background shell.** The export appends 10,242 Gaussians — a
  5×-subdivided icosphere (10·4⁵+2 vertices) — at radius ≈240, every one of
  them at the identical quantized alpha 253. They are an environment backdrop,
  not scene content. Because their alpha outranks most of the real scene, a
  naive top-N-by-opacity subset of this file is 94% backdrop; the `--aabb`
  crop exists to exclude them. Scene content itself lies within ≈0.25 units of
  the origin, so the two populations separate cleanly.
- **Container framing.** Single gzip member with no optional fields (`FLG` 0),
  zero `MTIME`, and `OS` byte 19 — a value outside the range RFC 1952 defines.
  The reader ignores all of it, which this asset confirms against a real
  producer.
- **Header.** SPZ v2, SH degree 3, `fractionalBits` 12, `flags` `0x01`
  (antialiased), `reserved` 0.

## Privacy review

The container carries no `FNAME`, `FCOMMENT`, or `FEXTRA` field, no non-zero
`MTIME`, and no bytes after the gzip trailer; the decompressed stream is
exactly the size its header implies, so nothing is appended to the payload.
SPZ v1-v3 has no geolocation field, and the coordinates are a local
object-centred model frame. The committed crop additionally excludes the
sparse tail of incidental surroundings that extends several units past the
subject in the full export.

## License

CC0-1.0. To the extent possible under law, the repository author dedicates
this asset (the committed subset and its source export) to the public domain.
Incidental depiction of the manufacturer's product branding is not a claim of
affiliation or endorsement.
