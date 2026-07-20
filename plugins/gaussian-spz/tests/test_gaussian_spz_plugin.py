# SPDX-License-Identifier: Apache-2.0
"""End-to-end assertions for the Gaussian SPZ SdfFileFormat.

The container and dequantization math are covered by the C++ unit tests; this
suite proves the plugin authors the same stage contract as gaussian-ply
(hierarchy, schema, metadata policy, default prim, stage metrics) and that the
stable GSPZ-**** catalog agrees with the codes the sources can emit.
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
           / "plugin" / "resources" / "gaussian-spz" / "diagnostics.json")

NEGATIVE_FIXTURES = {
    "not-spz.spz": "GSPZ-E001",
    "empty-points-v2.spz": "GSPZ-E002",
    "version-5.spz": "GSPZ-E003",
    "plaintext-v2.spz": "GSPZ-E004",
    "huge-count-v2.spz": "GSPZ-E005",
    "sh-degree-5-v2.spz": "GSPZ-E006",
    "truncated-payload-v2.spz": "GSPZ-E007",
    "bad-crc-v2.spz": "GSPZ-E008",
    "trailing-after-member-v2.spz": "GSPZ-E009",
    # Valid SPZ container, but degree 4 is beyond the shared model.
    "decode-degree4-v2.spz": "GSPZ-E011",
    # A non-finite float16 position surfaces from the decoder.
    "decode-nonfinite-v1.spz": "GSPZ-E012",
    # v4 is a real but unsupported container version, not corruption.
    "plaintext-v4.spz": "GSPZ-E003",
}


def close(a, b, epsilon=1.0e-5):
    return abs(a - b) <= epsilon


def check_stage_contract(name, expected_count, expected_degree):
    """The stage contract SPZ must share with PLY, byte-for-byte in structure."""
    stage = Usd.Stage.Open(str(FIXTURES / name))
    assert stage, f"failed to open {name}"
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Asset"), name
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y, name
    assert close(UsdGeom.GetStageMetersPerUnit(stage), 1.0), name

    asset = stage.GetPrimAtPath("/Asset")
    assert asset.GetTypeName() == "Xform", name
    assert asset.GetKind() == "component", name
    metadata = asset.GetCustomData().get("gs", {})
    assert metadata.get("sourceFormat") == "Gaussian Splatting SPZ", metadata
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


def check_known_values():
    """The exact values encoded into decode-degree1-v2.spz, seen through USD.

    Source is RUB; the authored stage is the model's RDF reference frame, so Y
    and Z are negated relative to the SPZ-native positions.
    """
    stage = Usd.Stage.Open(str(FIXTURES / "decode-degree1-v2.spz"))
    splat = stage.GetPrimAtPath("/Asset/Splat")
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    opacities = splat.GetAttribute("opacities").Get()
    coefficients = splat.GetAttribute(
        "radiance:sphericalHarmonicsCoefficients").Get()

    assert all(close(a, b, 1.0e-3) for a, b in zip(positions[0], (1.0, -2.0, 0.5)))
    assert all(close(a, b, 1.0e-3) for a, b in zip(positions[1], (-3.0, -0.25, -4.0)))
    assert all(close(a, b, 1.0e-3)
               for a, b in zip(scales[0], (1.0, math.exp(1.0), math.exp(-1.0))))
    assert close(opacities[0], 0.8, 1.0e-4)
    assert close(opacities[1], 0.6, 1.0e-4)

    # SH is authored DC-first per Gaussian, then rest coefficients. Gaussian 0
    # rest coefficient 0 carries the band-1 flip (sign -1): source (0.1,0.2,0.3)
    # -> (-0.1,-0.2,-0.3). Index 1 is the first rest triple after the DC term.
    assert all(close(a, b, 0.02) for a, b in zip(coefficients[1], (-0.1, -0.2, -0.3)))


def check_metadata_only():
    """Read(metadataOnly=true) authors the contract without decoding streams."""
    layer = Sdf.Layer.OpenAsAnonymous(
        str(FIXTURES / "decode-degree1-v2.spz"), metadataOnly=True)
    assert layer, "metadata-only open failed"
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == 2, gs
    assert gs.get("shDegree") == 1, gs
    assert gs.get("sourceFormat") == "Gaussian Splatting SPZ", gs

    degree = layer.GetAttributeAtPath(
        "/Asset/Splat.radiance:sphericalHarmonicsDegree")
    assert degree is not None and degree.default == 1

    for array_name in ("positions", "scales", "orientations", "opacities",
                       "radiance:sphericalHarmonicsCoefficients", "extent"):
        attr = layer.GetAttributeAtPath(f"/Asset/Splat.{array_name}")
        assert attr is None or attr.default is None, (
            f"metadata-only read authored {array_name}")


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
        seen_codes.update(re.findall(r"GSPZ-[EW]\d{3}", caught))

    unknown = seen_codes - catalog_codes
    assert not unknown, f"codes missing from diagnostics.json: {unknown}"


def load_catalog():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    assert catalog.get("schema") == "gaussian-spz.diagnostics/v1", catalog
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
    # file-triggerable fixture).
    src = pathlib.Path(__file__).parents[1] / "src"
    header_codes = set()
    for source in (src / "io" / "GaussianSpzDiagnostics.h",
                   src / "GaussianSpzFileFormat.cpp"):
        header_codes.update(
            re.findall(r'"(GSPZ-[EW]\d{3})"', source.read_text(encoding="utf-8")))
    assert header_codes == set(codes), (
        f"source-only: {header_codes - set(codes)}, "
        f"catalog-only: {set(codes) - header_codes}")
    return set(codes)


def check_corpus_asset(spz_path):
    """Tolerance-based semantic checks for a real SPZ corpus asset.

    Real assets have no hand-checkable golden values; the expected count and
    SH degree come from the asset's provenance record, and the attribute
    values are checked for semantic validity rather than exact content.
    """
    provenance_path = spz_path.with_suffix(spz_path.suffix + ".provenance.json")
    assert provenance_path.exists(), f"missing provenance for {spz_path.name}"
    provenance = json.loads(provenance_path.read_text(encoding="ascii"))
    expected_count = provenance["output"]["gaussian_count"]
    expected_degree = provenance["output"]["sh_degree"]

    stage = Usd.Stage.Open(str(spz_path))
    assert stage, f"failed to open {spz_path.name}"
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Asset")
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y
    assert close(UsdGeom.GetStageMetersPerUnit(stage), 1.0)

    asset = stage.GetPrimAtPath("/Asset")
    metadata = asset.GetCustomData().get("gs", {})
    assert metadata.get("sourceFormat") == "Gaussian Splatting SPZ", metadata
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
            == len(opacities) == expected_count), spz_path.name
    assert len(coefficients) == expected_count * (expected_degree + 1) ** 2

    assert all(math.isfinite(c) for p in positions for c in p)
    assert all(math.isfinite(c) for v in coefficients for c in v)
    # exp() of the quantized log scale is always positive; alpha is a byte
    # scaled by 1/255, so unlike the PLY sigmoid it does reach 0 and 1.
    assert all(c > 0.0 and math.isfinite(c) for s in scales for c in s)
    assert all(0.0 <= o <= 1.0 for o in opacities)
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

    # The crop recorded in the provenance record bounds the SPZ-native
    # positions; the stage is authored in RDF, which negates Y and Z. Checking
    # the authored positions against the flipped box proves the derivation and
    # the frame flip agree, and would catch a corpus asset regenerated with
    # other arguments. The check is on positions, not on extent: extent is a
    # conservative three-sigma bound that legitimately overruns the crop by the
    # splat radius.
    aabb = provenance["derivation"]["aabb"]
    if aabb is not None:
        native_low, native_high = aabb[:3], aabb[3:]
        bounds = [(native_low[0], native_high[0]),
                  (-native_high[1], -native_low[1]),
                  (-native_high[2], -native_low[2])]
        quantum = 2.0 ** -provenance["source"]["fractional_bits"]
        for p in positions:
            assert all(lo - quantum <= p[axis] <= hi + quantum
                       for axis, (lo, hi) in enumerate(bounds)), (
                spz_path.name, p, aabb)

    # The metadata-only path must agree with the provenance record without
    # decoding the payload.
    layer = Sdf.Layer.OpenAsAnonymous(str(spz_path), metadataOnly=True)
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == expected_count, gs
    assert gs.get("shDegree") == expected_degree, gs


def main():
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("spz"), "SPZ file format is not registered"

    catalog_codes = load_catalog()

    check_stage_contract("decode-degree1-v2.spz", 2, 1)
    check_stage_contract("decode-degree3-v2.spz", 2, 3)
    check_stage_contract("decode-v1.spz", 1, 0)
    check_stage_contract("decode-v3.spz", 1, 0)
    check_stage_contract("minimal-v1.spz", 1, 0)
    check_stage_contract("minimal-v2.spz", 1, 0)
    check_stage_contract("minimal-v3.spz", 1, 0)
    check_known_values()
    check_metadata_only()
    check_negative_fixtures(catalog_codes)

    corpus_assets = sorted(CORPUS.glob("*/*.spz")) if CORPUS.is_dir() else []
    for spz_path in corpus_assets:
        check_corpus_asset(spz_path)

    print("gaussian-spz integration tests: OK "
          f"({len(corpus_assets)} corpus assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
