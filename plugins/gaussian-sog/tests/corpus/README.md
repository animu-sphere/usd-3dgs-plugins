# Real-asset corpus

Small subsets of real Gaussian Splatting assets converted to bundled SOG,
committed for tolerance-based semantic tests. Synthetic fixtures in
[`../fixtures`](../fixtures) remain the primary exact-assertion coverage;
corpus assets supplement them and must not be the sole source of coverage
(design policy §17).

The admission rules are the same license, provenance, deterministic
derivation, and size rules as the [PLY corpus](../../../gaussian-ply/tests/corpus/README.md).
The committed SOG files are format conversions of the recorded PLY subsets,
made with the pinned SplatTransform release recorded in each provenance file.
The SOG converter's SH palette fit is intentionally recorded as partially
nondeterministic; the source PLY hash and every output-entry hash remain
available for provenance.

## Layout

```
corpus/
  <asset-name>/
    <asset-name>-<derivation>.sog                 committed bundled SOG
    <asset-name>-<derivation>.sog.provenance.json machine-readable record
    PROVENANCE.md                                 capture, conversion, license
```

`test_gaussian_sog_plugin.py` discovers every `corpus/*/*.sog`, reads the
expected Gaussian count and SH degree from the provenance record, and checks
the decoded stage semantically. Corpus assets are intentionally not declared
in `openstrata.plugin.yaml`, so they are not packaged into the distributed
bundle.

These assets are admitted as real SOG decoder inputs. They are not used for
cross-format equivalence testing; that coverage is provided by the synthetic
PLY/SPZ/SOG triples.
