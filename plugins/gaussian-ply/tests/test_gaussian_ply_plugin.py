# SPDX-License-Identifier: Apache-2.0
"""End-to-end assertions for the Gaussian PLY SdfFileFormat."""

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
           / "plugin" / "resources" / "gaussian-ply" / "diagnostics.json")

NEGATIVE_FIXTURES = {
    "mesh-not-gaussian.ply": "GSPLY-E001",
    "empty-vertex.ply": "GSPLY-E002",
    "missing-opacity.ply": "GSPLY-E003",
    "list-property.ply": "GSPLY-E004",
    "invalid-sh-name.ply": "GSPLY-E005",
    "duplicate-sh-index.ply": "GSPLY-E006",
    "non-contiguous-sh.ply": "GSPLY-E007",
    "sh-count-not-rgb.ply": "GSPLY-E008",
    "sh-invalid-degree.ply": "GSPLY-E009",
    "nan-opacity-binary-le.ply": "GSPLY-E010",
    "out-of-range-double.ply": "GSPLY-E010",
    "overflow-scale.ply": "GSPLY-E012",
    # The truncation failure stage is an implementation detail; any stable
    # error code is acceptable.
    "truncated-binary-le.ply": "GSPLY-E",
}


def close(a, b, epsilon=1.0e-5):
    return abs(a - b) <= epsilon


def check_stage(name, expected_degree):
    stage = Usd.Stage.Open(str(FIXTURES / name))
    assert stage, f"failed to open {name}"
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Asset")
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y
    assert close(UsdGeom.GetStageMetersPerUnit(stage), 1.0)

    asset = stage.GetPrimAtPath("/Asset")
    metadata = asset.GetCustomData().get("gs", {})
    assert metadata.get("sourceFormat") == "Gaussian Splatting PLY", metadata
    assert metadata.get("gaussianCount") == 1, metadata
    assert metadata.get("shDegree") == expected_degree, metadata

    splat = stage.GetPrimAtPath("/Asset/Splat")
    assert splat and splat.GetTypeName() == "ParticleField3DGaussianSplat", splat
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    orientations = splat.GetAttribute("orientations").Get()
    opacities = splat.GetAttribute("opacities").Get()
    coefficients = splat.GetAttribute(
        "radiance:sphericalHarmonicsCoefficients").Get()
    assert len(positions) == len(scales) == len(orientations) == len(opacities) == 1
    assert tuple(positions[0]) == (1.0, 2.0, 3.0)
    assert all(close(a, b) for a, b in zip(scales[0], (1.0, 2.0, 0.5)))
    assert close(orientations[0].GetReal(), 1.0)
    assert close(opacities[0], 0.5)
    assert len(coefficients) == (expected_degree + 1) ** 2
    assert len(splat.GetAttribute("extent").Get()) == 2


def check_three_gaussians():
    stage = Usd.Stage.Open(str(FIXTURES / "three-gaussian-binary-le.ply"))
    assert stage
    asset = stage.GetPrimAtPath("/Asset")
    metadata = asset.GetCustomData().get("gs", {})
    assert metadata.get("gaussianCount") == 3, metadata
    assert metadata.get("shDegree") == 1, metadata
    splat = stage.GetPrimAtPath("/Asset/Splat")
    positions = splat.GetAttribute("positions").Get()
    coefficients = splat.GetAttribute(
        "radiance:sphericalHarmonicsCoefficients").Get()
    assert len(positions) == 3
    assert len(coefficients) == 3 * 4
    # Interleaved per Gaussian: DC then rest. Gaussian 1 DC is (0.4, 0.5, 0.6).
    assert all(close(a, b) for a, b in zip(coefficients[4], (0.4, 0.5, 0.6)))


def check_metadata_only():
    """Read(metadataOnly=true) authors the stage contract without arrays."""
    layer = Sdf.Layer.OpenAsAnonymous(
        str(FIXTURES / "degree-3-sh.ply"), metadataOnly=True)
    assert layer, "metadata-only open failed"
    asset = layer.GetPrimAtPath("/Asset")
    assert asset is not None
    gs = asset.customData.get("gs", {})
    assert gs.get("gaussianCount") == 1, gs
    assert gs.get("shDegree") == 3, gs
    assert gs.get("sourceFormat") == "Gaussian Splatting PLY", gs

    degree = layer.GetAttributeAtPath(
        "/Asset/Splat.radiance:sphericalHarmonicsDegree")
    assert degree is not None and degree.default == 3

    for arrayName in ("positions", "scales", "orientations", "opacities",
                      "radiance:sphericalHarmonicsCoefficients", "extent"):
        attr = layer.GetAttributeAtPath(f"/Asset/Splat.{arrayName}")
        assert attr is None or attr.default is None, (
            f"metadata-only read authored {arrayName}")


def check_format_args():
    path = str(FIXTURES / "three-gaussian-binary-le.ply")

    identifier = Sdf.Layer.CreateIdentifier(
        path, {"opacityThreshold": "0.4", "scaleMultiplier": "2",
               "shDegree": "0"})
    stage = Usd.Stage.Open(identifier)
    assert stage, "stage with format args failed to open"
    metadata = stage.GetPrimAtPath("/Asset").GetCustomData().get("gs", {})
    assert metadata.get("gaussianCount") == 2, metadata
    assert metadata.get("shDegree") == 0, metadata
    splat = stage.GetPrimAtPath("/Asset/Splat")
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    coefficients = splat.GetAttribute(
        "radiance:sphericalHarmonicsCoefficients").Get()
    assert len(positions) == 2
    # sigmoid(-1) ~= 0.269 was filtered; the survivors keep source order.
    assert all(close(a, b) for a, b in zip(positions[0], (-4.0, 5.0, -6.0)))
    assert all(close(a, b) for a, b in zip(
        scales[0], (2 * math.exp(-1), 2.0, 2 * math.exp(0.25))))
    assert len(coefficients) == 2
    assert all(close(a, b) for a, b in zip(coefficients[1], (-0.7, 0.8, -0.9)))
    assert splat.GetAttribute(
        "radiance:sphericalHarmonicsDegree").Get() == 0

    # An invalid argument value is an error, not a silent default.
    bad = Sdf.Layer.CreateIdentifier(path, {"shDegree": "9"})
    caught = ""
    try:
        stage = Usd.Stage.Open(bad)
    except Tf.ErrorException as exc:
        stage = None
        caught = safe_error_text(exc)
    assert stage is None, "invalid shDegree was accepted"
    assert "GSPLY-E201" in caught, caught


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
        seen_codes.update(re.findall(r"GSPLY-[EW]\d{3}", caught))

    unknown = seen_codes - catalog_codes
    assert not unknown, f"codes missing from diagnostics.json: {unknown}"


def load_catalog():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    assert catalog.get("schema") == "gaussian-ply.diagnostics/v1", catalog
    codes = [entry["code"] for entry in catalog["diagnostics"]]
    assert len(codes) == len(set(codes)), "duplicate codes in diagnostics.json"
    for entry in catalog["diagnostics"]:
        assert entry.get("severity") in ("error", "warning"), entry
        assert entry.get("summary"), entry
        assert entry.get("action"), entry
    return set(codes)


def check_corpus_asset(ply_path):
    """Tolerance-based semantic checks for a real trained corpus asset.

    Real assets have no hand-checkable golden values; the expected count and
    SH degree come from the asset's provenance record, and the attribute
    values are checked for semantic validity rather than exact content.
    """
    provenance_path = ply_path.with_suffix(ply_path.suffix + ".provenance.json")
    assert provenance_path.exists(), f"missing provenance for {ply_path.name}"
    provenance = json.loads(provenance_path.read_text(encoding="ascii"))
    expected_count = provenance["output"]["gaussian_count"]
    expected_degree = provenance["output"]["sh_degree"]

    stage = Usd.Stage.Open(str(ply_path))
    assert stage, f"failed to open {ply_path.name}"
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Asset")
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y
    assert close(UsdGeom.GetStageMetersPerUnit(stage), 1.0)

    asset = stage.GetPrimAtPath("/Asset")
    metadata = asset.GetCustomData().get("gs", {})
    assert metadata.get("sourceFormat") == "Gaussian Splatting PLY", metadata
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
            == len(opacities) == expected_count), ply_path.name
    assert len(coefficients) == expected_count * (expected_degree + 1) ** 2

    assert all(math.isfinite(c) for p in positions for c in p)
    assert all(math.isfinite(c) for v in coefficients for c in v)
    # The decoder maps log scales through exp, opacity logits through the
    # sigmoid, and normalizes quaternions; check those semantic ranges.
    assert all(c > 0.0 and math.isfinite(c) for s in scales for c in s)
    assert all(0.0 < o < 1.0 for o in opacities)
    for q in orientations:
        norm = math.sqrt(q.GetReal() ** 2 + sum(c * c for c in q.GetImaginary()))
        assert close(norm, 1.0, 1.0e-3), q

    extent = splat.GetAttribute("extent").Get()
    assert len(extent) == 2
    low, high = extent
    assert all(low[i] <= high[i] for i in range(3)), extent
    epsilon = 1.0e-4
    for p in positions:
        assert all(low[i] - epsilon <= p[i] <= high[i] + epsilon
                   for i in range(3)), (p, extent)

    # The metadata-only path must agree with the provenance record without
    # decoding the payload.
    layer = Sdf.Layer.OpenAsAnonymous(str(ply_path), metadataOnly=True)
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == expected_count, gs
    assert gs.get("shDegree") == expected_degree, gs


def main():
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("ply"), "PLY file format is not registered"

    catalog_codes = load_catalog()

    check_stage("one-gaussian-ascii.ply", 0)
    check_stage("one-gaussian-binary-le.ply", 0)
    check_stage("degree-1-sh.ply", 1)
    check_stage("degree-2-sh.ply", 2)
    check_stage("degree-3-sh.ply", 3)
    check_stage("reordered-properties.ply", 1)
    check_three_gaussians()
    check_metadata_only()
    check_format_args()
    check_negative_fixtures(catalog_codes)

    corpus_assets = sorted(CORPUS.glob("*/*.ply")) if CORPUS.is_dir() else []
    for ply_path in corpus_assets:
        check_corpus_asset(ply_path)

    print("gaussian-ply integration tests: OK "
          f"({len(corpus_assets)} corpus assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
