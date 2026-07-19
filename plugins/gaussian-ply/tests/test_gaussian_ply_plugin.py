# SPDX-License-Identifier: Apache-2.0
"""End-to-end assertions for the Gaussian PLY SdfFileFormat."""

import json
import math
import os
import pathlib
import sys

from pxr import Plug, Sdf, Tf, Usd, UsdGeom


FIXTURES = pathlib.Path(__file__).parent / "fixtures"
CORPUS = pathlib.Path(__file__).parent / "corpus"


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


def main():
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("ply"), "PLY file format is not registered"

    check_stage("one-gaussian-ascii.ply", 0)
    check_stage("one-gaussian-binary-le.ply", 0)
    check_stage("degree-1-sh.ply", 1)

    for name in (
        "mesh-not-gaussian.ply",
        "missing-opacity.ply",
        "truncated-binary-le.ply",
    ):
        try:
            stage = Usd.Stage.Open(str(FIXTURES / name))
        except Tf.ErrorException:
            stage = None
        assert stage is None, name

    corpus_assets = sorted(CORPUS.glob("*/*.ply")) if CORPUS.is_dir() else []
    for ply_path in corpus_assets:
        check_corpus_asset(ply_path)

    print("gaussian-ply integration tests: OK "
          f"({len(corpus_assets)} corpus assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
