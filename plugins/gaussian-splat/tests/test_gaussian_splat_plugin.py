# SPDX-License-Identifier: Apache-2.0
"""End-to-end assertions for the Gaussian `.splat` SdfFileFormat.

The record layout and value decoding are covered by the C++ unit tests; this
suite proves the plugin authors the same stage contract as the other bundles
(hierarchy, schema, metadata policy, default prim, stage metrics), routes
renamed files to a specific diagnostic, and that the stable GSSPLAT-****
catalog agrees with the codes the sources can emit.
"""

import hashlib
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
           / "plugin" / "resources" / "gaussian-splat" / "diagnostics.json")

SOURCE_FORMAT = "Gaussian Splatting SPLAT"

NEGATIVE_FIXTURES = {
    "empty.splat": "GSSPLAT-E001",
    "partial-record.splat": "GSSPLAT-E002",
    # Files of other formats with a .splat extension are named, not decoded.
    "renamed-ply.splat": "GSSPLAT-E003",
    "renamed-ply-crlf.splat": "GSSPLAT-E003",
    "renamed-ply-odd-size.splat": "GSSPLAT-E003",
    "renamed-spz-v4.splat": "GSSPLAT-E003",
    "renamed-sog.splat": "GSSPLAT-E003",
    "renamed-glb.splat": "GSSPLAT-E003",
    "nonfinite-position.splat": "GSSPLAT-E006",
    "infinite-position.splat": "GSSPLAT-E006",
    "zero-scale.splat": "GSSPLAT-E007",
    "negative-scale.splat": "GSSPLAT-E007",
    "nonfinite-scale.splat": "GSSPLAT-E007",
}


def close(a, b, epsilon=1.0e-5):
    return abs(a - b) <= epsilon


def check_stage_contract(name, expected_count):
    """The stage contract `.splat` shares with every other format."""
    stage = Usd.Stage.Open(str(FIXTURES / name))
    assert stage, f"failed to open {name}"
    assert stage.GetDefaultPrim().GetPath() == Sdf.Path("/Asset"), name
    assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y, name
    assert close(UsdGeom.GetStageMetersPerUnit(stage), 1.0), name

    asset = stage.GetPrimAtPath("/Asset")
    assert asset.GetTypeName() == "Xform", name
    assert asset.GetKind() == "component", name
    metadata = asset.GetCustomData().get("gs", {})
    assert metadata.get("sourceFormat") == SOURCE_FORMAT, metadata
    assert metadata.get("gaussianCount") == expected_count, metadata
    assert metadata.get("shDegree") == 0, metadata

    splat = stage.GetPrimAtPath("/Asset/Splat")
    assert splat and splat.GetTypeName() == "ParticleField3DGaussianSplat", splat
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    orientations = splat.GetAttribute("orientations").Get()
    opacities = splat.GetAttribute("opacities").Get()
    coefficients = splat.GetAttribute(
        "radiance:sphericalHarmonicsCoefficients").Get()
    assert (len(positions) == len(scales) == len(orientations)
            == len(opacities) == len(coefficients) == expected_count), name
    assert splat.GetAttribute(
        "radiance:sphericalHarmonicsDegree").Get() == 0, name

    assert all(math.isfinite(c) for p in positions for c in p), name
    assert all(c > 0.0 and math.isfinite(c) for s in scales for c in s), name
    assert all(0.0 <= o <= 1.0 for o in opacities), name
    for q in orientations:
        norm = math.sqrt(q.GetReal() ** 2 + sum(c * c for c in q.GetImaginary()))
        assert close(norm, 1.0, 1.0e-3), (name, q)

    extent = splat.GetAttribute("extent").Get()
    assert len(extent) == 2, name
    low, high = extent
    for p in positions:
        assert all(low[i] - 1.0e-4 <= p[i] <= high[i] + 1.0e-4
                   for i in range(3)), (name, p, extent)
    return stage


def check_known_values():
    """decode-known.splat seen through USD: the stored RDF positions arrive
    with Y and Z negated (ADR 0001), scales and opacity unchanged."""
    stage = Usd.Stage.Open(str(FIXTURES / "decode-known.splat"))
    splat = stage.GetPrimAtPath("/Asset/Splat")
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    opacities = splat.GetAttribute("opacities").Get()
    orientations = splat.GetAttribute("orientations").Get()

    assert tuple(positions[0]) == (1.0, -2.0, 0.5), positions[0]
    assert tuple(positions[1]) == (-3.0, -0.25, -4.0), positions[1]
    assert all(close(a, b, 1.0e-7) for a, b in zip(scales[0], (0.5, 1.0, 2.0)))
    assert close(opacities[0], 0.8, 1.0e-6)
    assert close(opacities[1], 0.6, 1.0e-6)
    # 90 degrees about +Y stored in RDF is 90 degrees about -Y in RUB.
    q = orientations[1]
    assert close(q.GetReal(), 0.70710678, 1.0e-5), q
    assert all(close(a, b, 1.0e-5)
               for a, b in zip(q.GetImaginary(), (0.0, -0.70710678, 0.0))), q


def check_metadata_only():
    """Read(metadataOnly=true) authors the contract from the file size."""
    layer = Sdf.Layer.OpenAsAnonymous(
        str(FIXTURES / "decode-known.splat"), metadataOnly=True)
    assert layer, "metadata-only open failed"
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == 2, gs
    assert gs.get("shDegree") == 0, gs
    assert gs.get("sourceFormat") == SOURCE_FORMAT, gs

    for array_name in ("positions", "scales", "orientations", "opacities",
                       "radiance:sphericalHarmonicsCoefficients", "extent"):
        attr = layer.GetAttributeAtPath(f"/Asset/Splat.{array_name}")
        assert attr is None or attr.default is None, (
            f"metadata-only read authored {array_name}")


def check_routing():
    """`.splat` is routed by extension in any case; `.ksplat` and `.splatv`
    are other formats and are not claimed."""
    file_format = Sdf.FileFormat.FindByExtension("splat")
    assert file_format, "splat file format is not registered"
    assert file_format.formatId == "splat", file_format.formatId
    assert Sdf.FileFormat.FindByExtension("SPLAT") == file_format
    for other in ("ksplat", "splatv"):
        found = Sdf.FileFormat.FindByExtension(other)
        assert not found or found.formatId != "splat", other
    assert file_format.CanRead(str(FIXTURES / "decode-known.splat"))
    assert not file_format.CanRead(str(FIXTURES / "empty.splat"))
    assert not file_format.CanRead(str(FIXTURES / "partial-record.splat"))


def check_warning_fixture():
    """A zero rotation warns (GSSPLAT-W001, asserted by the C++ decoder test)
    but still imports, as identity."""
    mark = Tf.Error.Mark()
    stage = Usd.Stage.Open(str(FIXTURES / "zero-rotation.splat"))
    assert stage, "zero-rotation.splat must still import"
    q = stage.GetPrimAtPath("/Asset/Splat").GetAttribute(
        "orientations").Get()[0]
    assert q.GetReal() == 1.0 and tuple(q.GetImaginary()) == (0.0, 0.0, 0.0), q
    mark.Clear()


def check_write_is_unsupported():
    """gaussian-splat is read-only; export must fail with the stable code."""
    stage = Usd.Stage.Open(str(FIXTURES / "minimal-one.splat"))
    target = (pathlib.Path(os.environ.get("TEMP", "."))
              / "gaussian-splat-export.splat")
    caught = ""
    try:
        exported = stage.GetRootLayer().Export(str(target))
    except Tf.ErrorException as exc:
        exported = False
        caught = safe_error_text(exc)
    assert not exported, ".splat export must not succeed"
    assert "GSSPLAT-E201" in caught, caught


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
        seen_codes.update(re.findall(r"GSSPLAT-[EW]\d{3}", caught))

    unknown = seen_codes - catalog_codes
    assert not unknown, f"codes missing from diagnostics.json: {unknown}"


def load_catalog():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    assert catalog.get("schema") == "gaussian-splat.diagnostics/v1", catalog
    codes = [entry["code"] for entry in catalog["diagnostics"]]
    assert len(codes) == len(set(codes)), "duplicate codes in diagnostics.json"
    for entry in catalog["diagnostics"]:
        assert entry.get("severity") in ("error", "warning"), entry
        assert entry["severity"] == (
            "warning" if "-W" in entry["code"] else "error"), entry
        assert entry.get("summary"), entry
        assert entry.get("action"), entry

    # The catalog and the codes the sources can emit must agree exactly, in
    # both directions: a code in the header but absent from the catalog would
    # ship undocumented, and a catalog entry no source emits is dead.
    src = pathlib.Path(__file__).parents[1] / "src"
    header_codes = set()
    for source in (src / "io" / "GaussianSplatDiagnostics.h",
                   src / "GaussianSplatFileFormat.cpp"):
        header_codes.update(re.findall(
            r'"(GSSPLAT-[EW]\d{3})"', source.read_text(encoding="utf-8")))
    assert header_codes == set(codes), (
        f"source-only: {header_codes - set(codes)}, "
        f"catalog-only: {set(codes) - header_codes}")
    return set(codes)


def check_corpus_asset(splat_path):
    """Semantic checks for a CC0 corpus conversion, against its provenance
    record. Value agreement with the source PLY is the equivalence suite's
    job (tests/equivalence/test_splat_equivalence.cpp)."""
    provenance_path = splat_path.with_suffix(".splat.provenance.json")
    assert provenance_path.exists(), f"missing provenance for {splat_path.name}"
    provenance = json.loads(provenance_path.read_text(encoding="ascii"))
    output = provenance["output"]
    data = splat_path.read_bytes()
    assert len(data) == output["bytes"], splat_path.name
    assert hashlib.sha256(data).hexdigest() == output["sha256"], splat_path.name

    stage = Usd.Stage.Open(str(splat_path))
    assert stage, f"failed to open {splat_path.name}"
    metadata = stage.GetPrimAtPath("/Asset").GetCustomData().get("gs", {})
    assert metadata.get("sourceFormat") == SOURCE_FORMAT, metadata
    assert metadata.get("gaussianCount") == output["gaussian_count"], metadata
    assert metadata.get("shDegree") == output["sh_degree"], metadata

    splat = stage.GetPrimAtPath("/Asset/Splat")
    positions = splat.GetAttribute("positions").Get()
    scales = splat.GetAttribute("scales").Get()
    orientations = splat.GetAttribute("orientations").Get()
    opacities = splat.GetAttribute("opacities").Get()
    assert len(positions) == output["gaussian_count"]
    assert all(math.isfinite(c) for p in positions for c in p)
    assert all(c > 0.0 and math.isfinite(c) for s in scales for c in s)
    assert all(0.0 <= o <= 1.0 for o in opacities)
    for q in orientations:
        norm = math.sqrt(q.GetReal() ** 2 + sum(c * c for c in q.GetImaginary()))
        assert close(norm, 1.0, 1.0e-3), q

    layer = Sdf.Layer.OpenAsAnonymous(str(splat_path), metadataOnly=True)
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == output["gaussian_count"], gs


def main():
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))

    catalog_codes = load_catalog()

    check_routing()
    check_stage_contract("decode-known.splat", 2)
    check_stage_contract("minimal-one.splat", 1)
    check_stage_contract("zero-rotation.splat", 2)
    check_known_values()
    check_metadata_only()
    check_warning_fixture()
    check_write_is_unsupported()
    check_negative_fixtures(catalog_codes)

    corpus_assets = sorted(CORPUS.glob("*/*.splat")) if CORPUS.is_dir() else []
    for splat_path in corpus_assets:
        check_corpus_asset(splat_path)

    print("gaussian-splat integration tests: OK "
          f"({len(corpus_assets)} corpus assets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
