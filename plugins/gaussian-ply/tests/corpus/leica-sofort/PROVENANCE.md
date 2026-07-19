# leica-sofort

A real trained Gaussian Splatting asset: a Leica Sofort instant camera on a
desk in front of a keyboard.

## Capture

- 200 frames extracted from handheld video shot by the repository author on
  2026-07-19. The author owns the footage and the photographed object.
- Camera poses and the sparse point cloud were reconstructed with COLMAP
  3.10 (CUDA) following the Graphdeco `convert.py` pipeline: OPENCV
  single-camera feature extraction, exhaustive matching, mapping, and
  undistortion; 183 of 200 frames registered.

## Training

- Trainer: [Brush](https://github.com/ArthurBrussee/brush) v0.3.0
  (Apache-2.0), release binary
  `brush-app-x86_64-pc-windows-msvc.zip`, SHA-256 verified against the
  published checksum.
- Command: defaults plus `--total-steps 30000 --seed 42`; SH degree 3.
- Source export: `camera_02_30000.ply`, 145,912 Gaussians, 34,436,783 bytes,
  SHA-256 `dc5651d86d820aa30c0dadccda70f7bce9d7e80921e87a01e09909da692b0c98`.
  The full export is retained outside the repository; only the subset below
  is committed.

## Committed subset

- `leica-sofort-top8192.ply`: the 8,192 most opaque Gaussians of the source
  export, original vertex order and property order preserved, derived by
  `scripts/ply_subset.py --top-n 8192`. Exact hashes and parameters are in
  [`leica-sofort-top8192.ply.provenance.json`](leica-sofort-top8192.ply.provenance.json).

## Dialect notes

Brush exports binary little-endian PLY with the Graphdeco property set but
alphabetical property declaration order (`f_dc_*` first, `x`/`y`/`z` last)
and no `nx`/`ny`/`nz` normals. Together with
[`yashica-t4`](../yashica-t4/PROVENANCE.md) this gives the corpus two
independent captures through the same non-reference property ordering.

## License

CC0-1.0. To the extent possible under law, the repository author dedicates
this asset (the committed subset and its source export) to the public
domain. Incidental depiction of the manufacturer's product branding is not a
claim of affiliation or endorsement.
