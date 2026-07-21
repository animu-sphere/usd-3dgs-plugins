# gaussian-sog

The **v0.4.0 skeleton** for the v0.5.0 SOG v2 one-object importer
([release plan](../../docs/roadmap/release-plan.md#v050--sog-v2-one-object-import)).

There is no SOG decoding here yet. The bundle exists so that adding a third
format exercises the same declarative build, test, package, and CI path as
`gaussian-ply` and `gaussian-spz` *before* production decoder work begins:

- `ost plugin build|test|package plugins/gaussian-sog` work today;
- the PR lane runs the same three hosted cells as the other bundles
  (`openstrata.ci.yaml`), while its cells are marked `release: false` so no
  skeleton package ships;
- `.sog` files are recognized and rejected with the stable diagnostic
  `GSSOG-E001` ("SOG import is not implemented in this release") instead of
  USD reporting that no plugin was found — never opened as an empty stage.

The v0.5.0 scope, dependency decisions (ZIP reading, lossless WebP), and the
`GSSOG-****` catalog plan are recorded in
[docs/reference/SOG_FORMAT.md](../../docs/reference/SOG_FORMAT.md). The
decoder targets the shared contract in
[GAUSSIAN_MODEL_CONTRACT.md](../../docs/reference/GAUSSIAN_MODEL_CONTRACT.md)
and is developed against the decoder test kit
(`openstrata/gs/testing/DecoderTestKit.h`) following the contributor guide
for adding a format decoder.
