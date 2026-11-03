# SPDX-License-Identifier: Apache-2.0
"""Asset-resolver matrix for the unbundled SOG layout.

An unbundled `meta.json` names its WebP property planes relative to itself, so
every plane is a companion asset reached through the active ArResolver, anchored
on the layer's resolved path. This suite drives the stock ArDefaultResolver
through each row of docs/reference/RESOLVER_MATRIX.md that it can exercise
without a custom resolver plugin: how the primary asset is named (absolute,
cwd-relative, anchored reference, search path), how companions fail, and which
companion names are refused outright.

Each case copies the committed fixture into its own fresh directory, so layer
registry caching cannot let one case satisfy another.
"""

import os
import pathlib
import shutil
import sys
import tempfile

from pxr import Ar, Plug, Sdf, Tf, Usd


FIXTURES = pathlib.Path(__file__).parent / "fixtures"
SOURCE = FIXTURES / "unbundled-kit-multi-degree3"
EXPECTED_COUNT = 3


def safe_error_text(exc):
    try:
        return str(exc)
    except Exception:
        return " ".join(repr(a) for a in exc.args)


def copy_asset(target, rename=None):
    """A copy of the unbundled fixture at `target`; `rename` maps a companion
    file name to the name it is written under."""
    target.mkdir(parents=True, exist_ok=False)
    for source in SOURCE.iterdir():
        name = (rename or {}).get(source.name, source.name)
        shutil.copyfile(source, target / name)
    return target / "meta.json"


def gaussian_count(stage, prim_path):
    prim = stage.GetPrimAtPath(prim_path)
    assert prim, f"no prim at {prim_path}"
    positions = prim.GetAttribute("positions").Get()
    return len(positions) if positions is not None else 0


def expect_failure(open_call, code):
    caught = ""
    try:
        stage = open_call()
    except Tf.ErrorException as exc:
        stage = None
        caught = safe_error_text(exc)
    assert stage is None, f"expected {code}, but the open succeeded"
    assert code in caught, (code, caught)
    return caught


def write_referencing_layer(path, asset_path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "#usda 1.0\n"
        "(\n"
        '    defaultPrim = "Root"\n'
        ")\n\n"
        'def Xform "Root" (\n'
        f"    references = @{asset_path}@\n"
        ")\n"
        "{\n"
        "}\n",
        encoding="utf-8")


def filesystem_is_case_sensitive(directory):
    probe = directory / "case-probe"
    probe.write_bytes(b"")
    try:
        return not (directory / "CASE-PROBE").exists()
    finally:
        probe.unlink()


# --- rows of the matrix ------------------------------------------------------


def case_absolute(root):
    meta = copy_asset(root / "absolute")
    stage = Usd.Stage.Open(str(meta.resolve()))
    assert stage, "absolute open failed"
    assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT


def case_cwd_relative(root):
    copy_asset(root / "cwd" / "asset")
    previous = os.getcwd()
    os.chdir(root / "cwd")
    try:
        stage = Usd.Stage.Open(os.path.join("asset", "meta.json"))
    finally:
        os.chdir(previous)
    assert stage, "cwd-relative open failed"
    assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT


def case_anchored_references(root):
    """A referencing layer names the asset relative to itself; the plugin then
    anchors each plane on the *resolved* meta.json, not on the referencing
    layer."""
    copy_asset(root / "anchored" / "assets" / "cloud")
    for layer_path, asset_path in (
            (root / "anchored" / "assets" / "dot.usda", "./cloud/meta.json"),
            (root / "anchored" / "shots" / "parent.usda",
             "../assets/cloud/meta.json")):
        write_referencing_layer(layer_path, asset_path)
        stage = Usd.Stage.Open(str(layer_path))
        assert stage, f"failed to open {layer_path.name}"
        assert gaussian_count(stage, "/Root/Splat") == EXPECTED_COUNT, \
            layer_path.name

    # The same reference spelled as an absolute path with forward slashes,
    # which ArDefaultResolver accepts on every platform.
    absolute = (root / "anchored" / "assets" / "cloud" / "meta.json").resolve()
    layer_path = root / "anchored" / "absolute.usda"
    write_referencing_layer(layer_path, absolute.as_posix())
    stage = Usd.Stage.Open(str(layer_path))
    assert stage, "absolute reference failed"
    assert gaussian_count(stage, "/Root/Splat") == EXPECTED_COUNT


def case_search_path(root):
    """A search-path identifier (neither absolute nor ./ or ../) that only the
    resolver context can find: not beside the referencing layer, not under the
    current directory."""
    search_root = root / "search"
    copy_asset(search_root / "library" / "cloud")
    context = Ar.ResolverContext(
        Ar.DefaultResolverContext([str(search_root)]))

    layer_path = root / "search-shot" / "shot.usda"
    write_referencing_layer(layer_path, "library/cloud/meta.json")
    stage = Usd.Stage.Open(str(layer_path), context)
    assert stage, "search-path reference failed"
    assert gaussian_count(stage, "/Root/Splat") == EXPECTED_COUNT

    previous = os.getcwd()
    os.chdir(root / "search-shot")
    try:
        stage = Usd.Stage.Open("library/cloud/meta.json", context)
    finally:
        os.chdir(previous)
    assert stage, "search-path open failed"
    assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT


def case_missing_companion(root):
    meta = copy_asset(root / "missing")
    (meta.parent / "quats.webp").unlink()
    caught = expect_failure(lambda: Usd.Stage.Open(str(meta)), "GSSOG-E008")
    # The diagnostic names both the plane and the anchor it was resolved from.
    assert "quats.webp" in caught, caught
    assert "meta.json" in caught, caught


def case_missing_companion_is_not_searched(root):
    """A plane missing beside meta.json must stay missing. A bare plane name is
    a search-path identifier to ArDefaultResolver, so an unanchored lookup
    would find a same-named file in the current directory or on the search
    path and import another asset's plane. The impostors here are valid
    planes, so that mistake would make the open succeed."""
    search_root = root / "impostor-search"
    meta = copy_asset(search_root / "library" / "cloud")
    (meta.parent / "quats.webp").unlink()
    impostor = SOURCE / "quats.webp"
    shutil.copyfile(impostor, search_root / "quats.webp")
    working = root / "impostor-cwd"
    working.mkdir()
    shutil.copyfile(impostor, working / "quats.webp")

    context = Ar.ResolverContext(
        Ar.DefaultResolverContext([str(search_root)]))
    previous = os.getcwd()
    os.chdir(working)
    try:
        expect_failure(lambda: Usd.Stage.Open(str(meta), context),
                       "GSSOG-E008")
        expect_failure(lambda: Usd.Stage.Open("library/cloud/meta.json",
                                              context),
                       "GSSOG-E008")
    finally:
        os.chdir(previous)


def case_metadata_only_skips_companions(root):
    """Metadata-only reads must not open a property plane: with every
    companion gone, the metadata still comes from meta.json alone."""
    meta = copy_asset(root / "metadata-only")
    for plane in meta.parent.glob("*.webp"):
        plane.unlink()
    layer = Sdf.Layer.OpenAsAnonymous(str(meta), metadataOnly=True)
    assert layer, "metadata-only open failed without companions"
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == EXPECTED_COUNT, gs
    assert gs.get("shDegree") == 3, gs
    # ... while a full read of the same file fails on the first plane.
    expect_failure(lambda: Usd.Stage.Open(str(meta)), "GSSOG-E008")


def case_companion_case(root):
    """A companion whose on-disk name differs only in case. Resolution follows
    the host filesystem: found where names are case-insensitive (Windows and
    default macOS volumes), missing where they are case-sensitive."""
    meta = copy_asset(root / "case", rename={"scales.webp": "Scales.webp"})
    if filesystem_is_case_sensitive(meta.parent):
        expect_failure(lambda: Usd.Stage.Open(str(meta)), "GSSOG-E008")
    else:
        stage = Usd.Stage.Open(str(meta))
        assert stage, "case-insensitive filesystem did not resolve Scales.webp"
        assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT


def case_unsafe_companion_names():
    """Names that are paths or URIs are refused as malformed metadata on every
    platform, before the resolver is asked to interpret them."""
    for fixture in ("unbundled-backslash-plane-name",
                    "unbundled-uri-plane-name"):
        expect_failure(
            lambda: Usd.Stage.Open(str(FIXTURES / fixture / "meta.json")),
            "GSSOG-E005")


def case_windows_spellings(root):
    """On Windows the same primary asset opens under both separator styles.
    POSIX has one spelling; the backslash row does not apply there."""
    if os.name != "nt":
        return False
    meta = copy_asset(root / "windows").resolve()
    for spelling in (str(meta), meta.as_posix()):
        stage = Usd.Stage.Open(spelling)
        assert stage, f"failed to open {spelling}"
        assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT
    return True


def main():
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("sog"), \
        "SOG file format is not registered"

    with tempfile.TemporaryDirectory(prefix="gaussian-sog-resolver-") as tmp:
        root = pathlib.Path(tmp)
        case_absolute(root)
        case_cwd_relative(root)
        case_anchored_references(root)
        case_search_path(root)
        case_missing_companion(root)
        case_missing_companion_is_not_searched(root)
        case_metadata_only_skips_companions(root)
        case_companion_case(root)
        case_unsafe_companion_names()
        windows = case_windows_spellings(root)

    print("gaussian-sog resolver matrix: OK"
          + ("" if windows else " (Windows separator row not applicable)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
