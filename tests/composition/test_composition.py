# SPDX-License-Identifier: Apache-2.0
"""Exercise each import layout through OpenUSD composition arcs."""

from pathlib import Path
import shutil
import tempfile

from pxr import Gf, Kind, Sdf, Usd, UsdGeom, UsdUtils, UsdVol


PLUGINS = Path(__file__).resolve().parents[2] / "plugins"
FIXTURES = (
    ("gaussian-ply", "one-gaussian-binary-le.ply", ("PLY", 1, 0)),
    ("gaussian-spz", "decode-degree1-v2.spz", ("SPZ", 2, 1)),
    ("gaussian-sog", "kit-one-degree0.sog", ("SOG", 1, 0)),
    ("gaussian-sog", "unbundled-kit-multi-degree3/meta.json", ("SOG", 3, 3)),
    ("gaussian-splat", "decode-known.splat", ("SPLAT", 2, 0)),
)


def check_cloud(stage, path, expected):
    format_name, count, degree = expected
    assert not stage.GetCompositionErrors(), stage.GetCompositionErrors()
    root = stage.GetPrimAtPath(path)
    assert root.IsA(UsdGeom.Xform), path
    assert Usd.ModelAPI(root).GetKind() == Kind.Tokens.component, path
    assert root.GetCustomDataByKey("gs") == {
        "sourceFormat": f"Gaussian Splatting {format_name}",
        "gaussianCount": count,
        "shDegree": degree,
    }, path
    splat = UsdVol.ParticleField3DGaussianSplat.Get(stage, path + "/Splat")
    assert splat, path
    assert splat.GetRadianceSphericalHarmonicsDegreeAttr().Get() == degree, path
    positions = splat.GetPositionsAttr().Get()
    assert len(positions) == count, path
    return positions


def check_references(shot, source, expected):
    # Omit the prim path to exercise the source layer's defaultPrim.
    asset = "../assets/" + source.name
    shot.write_text(
        f"""#usda 1.0
def "Cloud" (prepend references = @{asset}@) {{}}
def "InstanceA" (
    prepend references = @{asset}@
    instanceable = true
) {{}}
def "InstanceB" (
    prepend references = @{asset}@
    instanceable = true
) {{}}
""", encoding="utf-8")
    stage = Usd.Stage.Open(str(shot), load=Usd.Stage.LoadNone)
    positions = check_cloud(stage, "/Cloud", expected)
    assert not stage.GetPrimAtPath("/Cloud").IsInstance()
    a = stage.GetPrimAtPath("/InstanceA")
    b = stage.GetPrimAtPath("/InstanceB")
    assert a.IsInstance() and b.IsInstance()
    assert a.GetPrototype() == b.GetPrototype()
    for prim in (a, b):
        path = str(prim.GetPath())
        assert stage.GetPrimAtPath(path + "/Splat").IsInstanceProxy()
        assert check_cloud(stage, path, expected) == positions
    translation = UsdGeom.Xformable(b).AddTranslateOp()
    assert translation.Set(Gf.Vec3d(1, 2, 3))
    assert not UsdGeom.Xformable(a).GetOrderedXformOps()
    assert a.GetPrototype() == b.GetPrototype()
    return positions


def write_payload(path, asset):
    path.write_text(
        f"""#usda 1.0
(
    defaultPrim = "Cloud"
    upAxis = "Y"
    metersPerUnit = 1
)
def "Cloud" (prepend payload = @{asset}@) {{}}
""", encoding="utf-8")


def check_payload(path, expected):
    stage = Usd.Stage.Open(str(path), load=Usd.Stage.LoadNone)
    assert stage, path
    root = stage.GetDefaultPrim()
    assert root.HasPayload(), path

    def check_unloaded():
        assert not stage.GetCompositionErrors(), stage.GetCompositionErrors()
        assert not root.IsLoaded(), path
        assert not stage.GetPrimAtPath("/Cloud/Splat"), path

    check_unloaded()
    unloaded_layers = set(stage.GetUsedLayers())
    stage.Load("/Cloud")
    assert root.IsLoaded(), path
    positions = check_cloud(stage, "/Cloud", expected)
    payload_layers = set(stage.GetUsedLayers()) - unloaded_layers
    assert len(payload_layers) == 1, payload_layers
    payload_layer, = payload_layers
    payload_path = payload_layer.realPath
    stage.Unload("/Cloud")
    check_unloaded()
    return positions, payload_path


def check_case(root, fixture, expected):
    assets = root / "assets"
    shot_dir = root / "shots"
    shot_dir.mkdir(parents=True)
    if fixture.name == "meta.json":
        shutil.copytree(fixture.parent, assets)
    else:
        assets.mkdir()
        shutil.copyfile(fixture, assets / fixture.name)
    source = assets / fixture.name

    # Open the payload before any references to the same source are live.
    shot = shot_dir / "payload.usda"
    write_payload(shot, "../assets/" + source.name)
    positions, payload_path = check_payload(shot, expected)
    assert Path(payload_path) == source
    reference_positions = check_references(shot_dir / "references.usda", source, expected)
    assert reference_positions == positions

    imported = Usd.Stage.Open(str(source))
    assert imported.GetDefaultPrim().GetPath() == Sdf.Path("/Asset")
    crate = assets / "cloud.usdc"
    assert imported.GetRootLayer().Export(str(crate))
    assembly = root / "assembly.usda"
    write_payload(assembly, "./assets/" + crate.name)
    package = root / "assembly.usdz"
    assert UsdUtils.CreateNewUsdzPackage(Sdf.AssetPath(str(assembly)), str(package))
    packaged_positions, payload_path = check_payload(package, expected)
    assert packaged_positions == positions
    assert payload_path.endswith("[assets/cloud.usdc]"), payload_path


def main():
    with tempfile.TemporaryDirectory(prefix="gaussian-usd-composition-") as tmp:
        root = Path(tmp).resolve()
        for bundle, name, expected in FIXTURES:
            fixture = PLUGINS / bundle / "tests" / "fixtures" / name
            check_case(root / bundle / fixture.stem, fixture, expected)
            print(f"Verified USD composition for {bundle}/{name}", flush=True)


if __name__ == "__main__":
    main()
