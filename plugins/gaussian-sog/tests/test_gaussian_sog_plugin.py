# SPDX-License-Identifier: Apache-2.0
"""End-to-end assertions for the Gaussian SOG SdfFileFormat.

The container mechanics and the dequantization math are covered by the C++ unit
tests; this suite proves the plugin authors the same stage contract as
gaussian-ply and gaussian-spz (hierarchy, schema, metadata policy, default prim,
stage metrics), that both SOG layouts reach it, that the broad `.json`
registration claims only real SOG metadata documents, and that the stable
GSSOG-**** catalog agrees with the codes the sources can emit.
"""

import json
import math
import os
import pathlib
import re
import sys

from pxr import Plug, Sdf, Tf, Usd, UsdGeom


FIXTURES = pathlib.Path(__file__).parent / "fixtures"
CORPUS = pathlib.Path(__file__).parent / "corpus"
CATALOG = (pathlib.Path(__file__).parents[1]
           / "plugin" / "resources" / "gaussian-sog" / "diagnostics.json")

NEGATIVE_FIXTURES = {
    "not-sog.sog": "GSSOG-E002",
    "no-meta.sog": "GSSOG-E002",
    "bad-zip.sog": "GSSOG-E004",
    "malformed-meta.sog": "GSSOG-E005",
    # Legacy v1 (no version field) and a future v3 are both unsupported
    # versions, not corruption.
    "version-1.sog": "GSSOG-E003",
    "version-3.sog": "GSSOG-E003",
    "count-exceeds-plane.sog": "GSSOG-E006",
    "bad-bands.sog": "GSSOG-E007",
    "missing-plane.sog": "GSSOG-E008",
    # A lossy plane is rejected rather than decoded approximately; a corrupt
    # bitstream fails the same way.
    "lossy-plane.sog": "GSSOG-E009",
    "corrupt-plane.sog": "GSSOG-E009",
    "short-codebook.sog": "GSSOG-E011",
    # Valid SOG, but a zero-Gaussian cloud is not an importable model.
    "empty-count.sog": "GSSOG-E012",
    "bad-quat-tag.sog": "GSSOG-E013",
    # A plane name that tries to escape its directory is malformed metadata,
    # and so is one naming a Windows character device.
    "escaping-plane-name.sog": "GSSOG-E005",
    "device-plane-name.sog": "GSSOG-E005",
    # Integral JSON numbers past the range of a 64-bit integer still reach the
    # diagnostic that reports them, with the code the value's own range check
    # chose.
    "huge-version.sog": "GSSOG-E003",
    "huge-count.sog": "GSSOG-E006",
    "huge-bands.sog": "GSSOG-E007",
    "huge-palette-count.sog": "GSSOG-E005",
}


def close(a, b, epsilon=1.0e-5):
    return abs(a - b) <= epsilon


def check_stage_contract(name, expected_count, expected_degree):
    """The stage contract SOG must share with PLY and SPZ, structurally exact."""
    stage = Usd.Stage.Open(str(FIXTURES / name))
    assert stage, f"failed to open {name}"
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Asset"), name
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y, name
    assert close(UsdGeom.GetStageMetersPerUnit(stage), 1.0), name

    asset = stage.GetPrimAtPath("/Asset")
    assert asset.GetTypeName() == "Xform", name
    assert asset.GetKind() == "component", name
    metadata = asset.GetCustomData().get("gs", {})
    assert metadata.get("sourceFormat") == "Gaussian Splatting SOG", metadata
    assert metadata.get("gaussianCount") == expected_count, metadata
    assert metadata.get("shDegree") == expected_degree, metadata

    splat = stage.GetPrimAtPath("/Asset/Splat")
    assert splat and splat.GetTypeName() == "ParticleField3DGaussianSplat", splat
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    orientations = splat.GetAttribute("orientations").Get()
    opacities = splat.GetAttribute("opacities").Get()
    coefficients = splat.GetAttribute(
        "radiance:sphericalHarmonicsCoefficients").Get()
    assert (len(positions) == len(scales) == len(orientations)
            == len(opacities) == expected_count), name
    assert len(coefficients) == expected_count * (expected_degree + 1) ** 2, name
    assert splat.GetAttribute(
        "radiance:sphericalHarmonicsDegree").Get() == expected_degree, name

    # Semantic ranges the decoder guarantees for any valid asset.
    assert all(math.isfinite(c) for p in positions for c in p), name
    assert all(c > 0.0 and math.isfinite(c) for s in scales for c in s), name
    assert all(0.0 <= o <= 1.0 for o in opacities), name
    for q in orientations:
        norm = math.sqrt(q.GetReal() ** 2 + sum(c * c for c in q.GetImaginary()))
        assert close(norm, 1.0, 1.0e-3), (name, q)

    extent = splat.GetAttribute("extent").Get()
    assert len(extent) == 2, name
    low, high = extent
    epsilon = 1.0e-4
    for p in positions:
        assert all(low[i] - epsilon <= p[i] <= high[i] + epsilon
                   for i in range(3)), (name, p, extent)
    return stage


def check_layouts_agree():
    """The bundled and unbundled layouts are two containers for one asset, so
    they must author the same stage. The unbundled read also exercises the
    resolver-backed companion loading of the property planes."""
    bundled = Usd.Stage.Open(str(FIXTURES / "kit-multi-degree3.sog"))
    unbundled = Usd.Stage.Open(
        str(FIXTURES / "unbundled-kit-multi-degree3" / "meta.json"))
    assert bundled and unbundled

    for name in ("positions", "scales", "orientations", "opacities",
                 "radiance:sphericalHarmonicsCoefficients"):
        left = bundled.GetPrimAtPath("/Asset/Splat").GetAttribute(name).Get()
        right = unbundled.GetPrimAtPath("/Asset/Splat").GetAttribute(name).Get()
        assert len(left) == len(right), name
        for a, b in zip(left, right):
            if hasattr(a, "GetReal"):
                assert close(a.GetReal(), b.GetReal(), 1.0e-6), name
                assert all(close(x, y, 1.0e-6)
                           for x, y in zip(a.GetImaginary(), b.GetImaginary())), name
            elif hasattr(a, "__len__"):
                assert all(close(x, y, 1.0e-6) for x, y in zip(a, b)), name
            else:
                assert close(a, b, 1.0e-6), name


def check_known_values():
    """The exact values encoded into decode-degree1.sog, seen through USD.

    SOG stores PLY-native (Graphdeco RDF) columns, so the fixture generator
    negated Y and Z going in and the decoder negates them back: these are the
    model-frame values (ADR 0001, SOG_MAPPING.md §5).
    """
    stage = Usd.Stage.Open(str(FIXTURES / "decode-degree1.sog"))
    splat = stage.GetPrimAtPath("/Asset/Splat")
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    opacities = splat.GetAttribute("opacities").Get()
    coefficients = splat.GetAttribute(
        "radiance:sphericalHarmonicsCoefficients").Get()

    assert all(close(a, b, 1.0e-3) for a, b in zip(positions[0], (1.0, 2.0, -0.5)))
    assert all(close(a, b, 1.0e-3) for a, b in zip(positions[1], (-3.0, 0.25, 4.0)))
    assert all(close(a, b, 1.0e-3)
               for a, b in zip(scales[0], (1.0, math.exp(1.0), math.exp(-1.0))))
    assert close(opacities[0], 0.8, 3.0e-3)
    assert close(opacities[1], 0.6, 3.0e-3)

    # SH is authored DC-first per Gaussian, then the rest coefficients, so
    # index 0 is the raw band-0 term and index 1 the first rest triple.
    assert all(close(a, b, 1.0e-4) for a, b in zip(coefficients[0], (0.0, 0.5, -0.5)))
    assert all(close(a, b, 1.0e-4) for a, b in zip(coefficients[1], (0.1, 0.2, 0.3)))


def check_metadata_only():
    """Read(metadataOnly=true) authors the contract from meta.json alone, with
    no WebP plane decoded."""
    layer = Sdf.Layer.OpenAsAnonymous(
        str(FIXTURES / "decode-degree1.sog"), metadataOnly=True)
    assert layer, "metadata-only open failed"
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == 2, gs
    assert gs.get("shDegree") == 1, gs
    assert gs.get("sourceFormat") == "Gaussian Splatting SOG", gs

    degree = layer.GetAttributeAtPath(
        "/Asset/Splat.radiance:sphericalHarmonicsDegree")
    assert degree is not None and degree.default == 1

    for array_name in ("positions", "scales", "orientations", "opacities",
                       "radiance:sphericalHarmonicsCoefficients", "extent"):
        attr = layer.GetAttributeAtPath(f"/Asset/Splat.{array_name}")
        assert attr is None or attr.default is None, (
            f"metadata-only read authored {array_name}")


def check_routing():
    """Both extensions resolve here, and the broad `.json` registration claims
    only documents that actually parse as SOG v2 metadata."""
    sog_format = Sdf.FileFormat.FindByExtension("sog")
    json_format = Sdf.FileFormat.FindByExtension("json")
    assert sog_format, "no file format registered for .sog"
    assert json_format, "no file format registered for .json"
    assert json_format.formatId == sog_format.formatId

    assert sog_format.CanRead(str(FIXTURES / "kit-one-degree0.sog"))
    # A ZIP whose contents are wrong is still claimed, so Read() can explain
    # the failure instead of USD reporting that no plugin was found.
    assert sog_format.CanRead(str(FIXTURES / "bad-zip.sog"))
    assert not sog_format.CanRead(str(FIXTURES / "not-sog.sog"))

    assert json_format.CanRead(
        str(FIXTURES / "unbundled-kit-multi-degree3" / "meta.json"))
    assert not json_format.CanRead(str(FIXTURES / "not-sog.json"))


def check_warning_fixture():
    """A palette label past the palette warns (GSSOG-W001) but still imports."""
    mark = Tf.Error.Mark()
    stage = Usd.Stage.Open(str(FIXTURES / "labels-out-of-range.sog"))
    assert stage, "labels-out-of-range.sog must still import"
    splat = stage.GetPrimAtPath("/Asset/Splat")
    assert splat.GetAttribute("radiance:sphericalHarmonicsDegree").Get() == 1
    mark.Clear()


def check_write_is_unsupported():
    """gaussian-sog is read-only; export must fail with the stable code."""
    stage = Usd.Stage.Open(str(FIXTURES / "kit-one-degree0.sog"))
    target = pathlib.Path(os.environ.get("TEMP", ".")) / "gaussian-sog-export.sog"
    caught = ""
    try:
        exported = stage.GetRootLayer().Export(str(target))
    except Tf.ErrorException as exc:
        exported = False
        caught = safe_error_text(exc)
    assert not exported, "SOG export must not succeed"
    assert "GSSOG-E201" in caught, caught


def safe_error_text(exc):
    try:
        return str(exc)
    except Exception:
        return " ".join(safe_repr(a) for a in exc.args)


def safe_repr(value):
    try:
        return repr(value)
    except Exception:
        return "<unprintable>"


def check_negative_fixtures(catalog_codes):
    seen_codes = set()
    for name, expected_code in NEGATIVE_FIXTURES.items():
        caught = ""
        try:
            stage = Usd.Stage.Open(str(FIXTURES / name))
        except Tf.ErrorException as exc:
            stage = None
            caught = safe_error_text(exc)
        assert stage is None, name
        assert expected_code in caught, (name, expected_code, caught)
        seen_codes.update(re.findall(r"GSSOG-[EW]\d{3}", caught))

    # The unbundled layout with an absent companion plane.
    caught = ""
    try:
        stage = Usd.Stage.Open(
            str(FIXTURES / "unbundled-missing-plane" / "meta.json"))
    except Tf.ErrorException as exc:
        stage = None
        caught = safe_error_text(exc)
    assert stage is None
    assert "GSSOG-E008" in caught, caught
    seen_codes.update(re.findall(r"GSSOG-[EW]\d{3}", caught))

    unknown = seen_codes - catalog_codes
    assert not unknown, f"codes missing from diagnostics.json: {unknown}"


def load_catalog():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    assert catalog.get("schema") == "gaussian-sog.diagnostics/v1", catalog
    codes = [entry["code"] for entry in catalog["diagnostics"]]
    assert len(codes) == len(set(codes)), "duplicate codes in diagnostics.json"
    for entry in catalog["diagnostics"]:
        assert entry.get("severity") in ("error", "warning"), entry
        assert entry.get("summary"), entry
        assert entry.get("action"), entry

    # The catalog and the codes the sources can emit must agree exactly, in
    # both directions: a code in the header but absent from the catalog would
    # ship undocumented, and a catalog entry no source emits is dead. Negative
    # fixtures alone cannot catch either (authoring-failure codes have no
    # file-triggerable fixture). GSSOG-E001 is retired with the v0.4.0
    # skeleton, so it must appear in neither.
    src = pathlib.Path(__file__).parents[1] / "src"
    source_codes = set()
    for source in (src / "io" / "GaussianSogDiagnostics.h",
                   src / "GaussianSogFileFormat.cpp"):
        source_codes.update(
            re.findall(r'"(GSSOG-[EW]\d{3})"', source.read_text(encoding="utf-8")))
    assert "GSSOG-E001" not in source_codes, "retired code was reused"
    assert source_codes == set(codes), (
        f"source-only: {source_codes - set(codes)}, "
        f"catalog-only: {set(codes) - source_codes}")
    return set(codes)


def check_corpus_asset(sog_path):
    """Tolerance-based semantic checks for a real SOG corpus asset.

    Real assets have no hand-checkable golden values; the expected count and
    SH degree come from the asset's provenance record, and the attribute
    values are checked for semantic validity rather than exact content.
    """
    provenance_path = sog_path.with_suffix(sog_path.suffix + ".provenance.json")
    assert provenance_path.exists(), f"missing provenance for {sog_path.name}"
    provenance = json.loads(provenance_path.read_text(encoding="ascii"))
    expected_count = provenance["output"]["gaussian_count"]
    expected_degree = provenance["output"]["sh_degree"]
    check_stage_contract(
        str(sog_path.relative_to(FIXTURES))
        if sog_path.is_relative_to(FIXTURES) else sog_path,
        expected_count, expected_degree)


def main():
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("sog"), "SOG file format is not registered"

    catalog_codes = load_catalog()

    check_routing()
    check_stage_contract("kit-one-degree0.sog", 1, 0)
    check_stage_contract("kit-multi-degree3.sog", 3, 3)
    check_stage_contract("kit-multi-degree3-deflated.sog", 3, 3)
    check_stage_contract("decode-degree1.sog", 2, 1)
    check_stage_contract("unbundled-kit-multi-degree3/meta.json", 3, 3)
    check_layouts_agree()
    check_known_values()
    check_metadata_only()
    check_warning_fixture()
    check_write_is_unsupported()
    check_negative_fixtures(catalog_codes)

    corpus_assets = sorted(CORPUS.glob("*/*.sog")) if CORPUS.is_dir() else []
    for sog_path in corpus_assets:
        check_corpus_asset(sog_path)

    print("gaussian-sog integration tests: OK "
          f"({len(corpus_assets)} corpus assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
