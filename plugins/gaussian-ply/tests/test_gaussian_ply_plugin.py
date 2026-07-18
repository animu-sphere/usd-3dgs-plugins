# SPDX-License-Identifier: Apache-2.0
"""End-to-end assertions for the Gaussian PLY SdfFileFormat."""

import math
import os
import pathlib
import sys

from pxr import Plug, Sdf, Tf, Usd, UsdGeom


FIXTURES = pathlib.Path(__file__).parent / "fixtures"


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

    print("gaussian-ply integration tests: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
