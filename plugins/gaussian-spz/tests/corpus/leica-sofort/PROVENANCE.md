# leica-sofort (SPZ)

A real Gaussian Splatting asset in SPZ form: a Leica Sofort instant camera.
The [PLY corpus](../../../../gaussian-ply/tests/corpus/leica-sofort/PROVENANCE.md)
holds a same-subject asset from an independent capture and a different
pipeline; the two are **not** two encodings of one reconstruction and must not
be compared value-for-value.

## Capture and reconstruction

- Captured and reconstructed on-device with
  [Scaniverse](https://scaniverse.com/) (Niantic), which both scans and trains
  the splat; there is no separate trainer stage and no COLMAP step.
  _(Pending: app version, device model, capture date.)_
- The repository author owns the capture and the photographed object.
- Source export: `leica_sofort.spz`, 171,397 Gaussians, 3,991,056 bytes,
  SHA-256
  `89894d8756ff4dbbf5d5a0050b71181310bd56cb9f831b4732b721a0b253be02`
  (decompressed payload 10,969,424 bytes, SHA-256
  `90a1fab0f521352f1e5cd1a9197af0dd1503a85fc5394934e674950fd3f1f203`).
  The full export is retained outside the repository; only the subset below is
  committed.

## Committed subset

- `leica-sofort-top8192.spz`: the 8,192 most opaque Gaussians inside the
  axis-aligned box `[-0.25, 0.25]³` of the source export, original point order
  preserved, derived by
  `scripts/spz_subset.py --top-n 8192 --aabb=-0.25,-0.25,-0.25,0.25,0.25,0.25`.
  The crop runs before the opacity ranking and is required — see the dialect
  notes. 153,774 of the 171,397 source Gaussians survive it.
- Selection copies the retained per-point records **byte-for-byte** out of the
  quantized planes, so the subset dequantizes to exactly the values the
  corresponding source points do. Exact hashes and parameters are in
  [`leica-sofort-top8192.spz.provenance.json`](leica-sofort-top8192.spz.provenance.json).

## Dialect notes

This export shows the same producer traits as
[`yashica-t4`](../yashica-t4/PROVENANCE.md) — the 10,242-point far-field
icosphere at radius ≈240 with uniform quantized alpha 253, `FLG` 0 / zero
`MTIME` / `OS` 19 gzip framing, and an SPZ v2 header at SH degree 3 with
`fractionalBits` 12 and `flags` `0x01`. Two independent captures agreeing on
all of it is what makes them dialect traits rather than one-off accidents.

The denser scene here makes the ranking behaviour clearer: 8,192 of this
file's Gaussians reach alpha 253 on scene content alone, so an uncropped
top-N would be a near-tie decided by index order. The crop removes that
fragility as well as the backdrop.

## Privacy review

The container carries no `FNAME`, `FCOMMENT`, or `FEXTRA` field, no non-zero
`MTIME`, and no bytes after the gzip trailer; the decompressed stream is
exactly the size its header implies, so nothing is appended to the payload.
SPZ v1-v3 has no geolocation field, and the coordinates are a local
object-centred model frame. The committed crop additionally excludes the
sparse tail of incidental surroundings, which in this capture reaches roughly
7 units from the subject.

## License

CC0-1.0. To the extent possible under law, the repository author dedicates
this asset (the committed subset and its source export) to the public domain.
Incidental depiction of the manufacturer's product branding is not a claim of
affiliation or endorsement.
