# yashica-t4

A real trained Gaussian Splatting asset: a Yashica T4 compact film camera on
a wooden table.

## Capture

- 143 frames extracted from handheld video shot by the repository author on
  2026-07-19. The author owns the footage and the photographed object.
- Camera poses and the sparse point cloud were reconstructed with COLMAP
  using the Graphdeco `convert.py` layout (undistorted images plus
  `sparse/0`).

## Training

- Trainer: [Brush](https://github.com/ArthurBrussee/brush) v0.3.0
  (Apache-2.0), release binary
  `brush-app-x86_64-pc-windows-msvc.zip`, SHA-256 verified against the
  published checksum.
- Command: defaults plus `--total-steps 30000 --seed 42`; SH degree 3.
- Source export: `camera_01_30000.ply`, 59,559 Gaussians, 14,057,474 bytes,
  SHA-256 `12d644e1a13c54bed4692a0444229653fd69135024946d74473173332e7f8543`.
  The full export is retained outside the repository; only the subset below
  is committed.

## Committed subset

- `yashica-t4-top8192.ply`: the 8,192 most opaque Gaussians of the source
  export, original vertex order and property order preserved, derived by
  `scripts/ply_subset.py --top-n 8192`. Exact hashes and parameters are in
  [`yashica-t4-top8192.ply.provenance.json`](yashica-t4-top8192.ply.provenance.json).

## Dialect notes

Brush exports binary little-endian PLY with the Graphdeco property set but
alphabetical property declaration order (`f_dc_*` first, `x`/`y`/`z` last)
and no `nx`/`ny`/`nz` normals. This makes the asset a real-world sample of a
non-reference property ordering for the dialect-compatibility work.

## License

CC0-1.0. To the extent possible under law, the repository author dedicates
this asset (the committed subset and its source export) to the public
domain. Incidental depiction of the manufacturer's product branding is not a
claim of affiliation or endorsement.
