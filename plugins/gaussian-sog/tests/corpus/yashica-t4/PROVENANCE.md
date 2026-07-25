# yashica-t4 (SOG)

A bundled SOG conversion of the provenance-recorded Yashica T4 Gaussian
asset in the [PLY corpus](../../../../gaussian-ply/tests/corpus/yashica-t4/PROVENANCE.md).
The committed file is admitted as a real SOG decoder input; it is not used as
a cross-format equivalence fixture.

## Conversion

- Source subset: `yashica-t4-top8192.ply`, 8,192 Gaussians, SH degree 3,
  SHA-256 `42537f23b36c093e96b4378845f0d86f77d755b7a611434aa95edbb4fcf53cde`.
- Converter: `@playcanvas/splat-transform` v3.1.6, commit `04b6d15`,
  command `splat-transform yashica-t4-top8192.ply yashica-t4-top8192.sog`.
- Conversion defaults were `--sh-iterations 10`, `--max-workers 4`, GPU
  adapter 0, on an NVIDIA RTX A5000. All source Gaussians are retained in
  source order; no filtering, decimation, or Morton reordering was applied.
- Output: SOG v2 bundled layout, 8,192 Gaussians, SH degree 3, 457,857 bytes,
  SHA-256 `0e6f606a60c76f7d78efdcbe8a400a0a6ba7d31bc172c7a39dbc3623ef9ff104`.

The SH-N palette uses randomized k-means initialization, so the palette and
its metadata are not byte-for-byte reproducible. The reproducible and
nondeterministic archive entries are listed in the adjacent JSON provenance
record.

## License

CC0-1.0. To the extent possible under law, the repository author dedicates
this asset and its source capture to the public domain. Incidental depiction
of the manufacturer's product branding is not a claim of affiliation or
endorsement.
