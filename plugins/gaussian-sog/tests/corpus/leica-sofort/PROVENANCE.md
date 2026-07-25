# leica-sofort (SOG)

A bundled SOG conversion of the provenance-recorded Leica Sofort Gaussian
asset in the [PLY corpus](../../../../gaussian-ply/tests/corpus/leica-sofort/PROVENANCE.md).
The committed file is admitted as a real SOG decoder input; it is not used as
a cross-format equivalence fixture.

## Conversion

- Source subset: `leica-sofort-top8192.ply`, 8,192 Gaussians, SH degree 3,
  SHA-256 `853cd6d8f1c59c471f5a53333d752282daea42ff4f70b89fb1b7d9d7f968d847`.
- Converter: `@playcanvas/splat-transform` v3.1.6, commit `04b6d15`,
  command `splat-transform leica-sofort-top8192.ply leica-sofort-top8192.sog`.
- Conversion defaults were `--sh-iterations 10`, `--max-workers 4`, GPU
  adapter 0, on an NVIDIA RTX A5000. All source Gaussians are retained in
  source order; no filtering, decimation, or Morton reordering was applied.
- Output: SOG v2 bundled layout, 8,192 Gaussians, SH degree 3, 462,511 bytes,
  SHA-256 `58b9d1db0e15af28683e064d47cb76ddffe0feca154bd4d38abf889d18687384`.

The SH-N palette uses randomized k-means initialization, so the palette and
its metadata are not byte-for-byte reproducible. The reproducible and
nondeterministic archive entries are listed in the adjacent JSON provenance
record.

## License

CC0-1.0. To the extent possible under law, the repository author dedicates
this asset and its source capture to the public domain. Incidental depiction
of the manufacturer's product branding is not a claim of affiliation or
endorsement.
