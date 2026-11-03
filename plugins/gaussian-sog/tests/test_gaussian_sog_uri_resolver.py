# SPDX-License-Identifier: Apache-2.0
"""Custom-resolver rows of the asset-resolver matrix for gaussian-sog.

test_gaussian_sog_resolver.py drives the stock ArDefaultResolver. This suite
covers the rows of docs/reference/RESOLVER_MATRIX.md that need a resolver
plugin of their own, using the test-only `gstest:` URI resolver
(uri_resolver/GsTestUriResolver.cpp) that CTest builds and registers for this
test alone:

- a URI-style primary identifier, opened directly, by reference, and
  metadata-only, for both layouts;
- companion planes served by that resolver, anchored on the URI;
- assets that serve a stream only (GetBuffer() returns null);
- a missing companion that stays missing when a same-named file exists
  elsewhere.

The resolver maps `gstest:<path>` to `<GSTEST_URI_RESOLVER_ROOT>/<path>` and
appends `open <identifier>` and `buffer <identifier>` to
GSTEST_URI_RESOLVER_LOG, which is how this suite observes what the plugin
asked the resolver for. Each case uses its own store directory, so the layer
registry cannot let one case satisfy another.
"""

import os
import pathlib
import shutil
import sys
import tempfile

from pxr import Plug, Sdf, Tf, Usd


FIXTURES = pathlib.Path(__file__).parent / "fixtures"
SOURCE = FIXTURES / "unbundled-kit-multi-degree3"
BUNDLED = FIXTURES / "kit-multi-degree3.sog"
EXPECTED_COUNT = 3
PLANES = sorted(p.name for p in SOURCE.iterdir() if p.suffix == ".webp")


def required_env(name):
    value = os.environ.get(name)
    assert value, f"{name} is not set; run this test through CTest"
    return pathlib.Path(value)


STORE = required_env("GSTEST_URI_RESOLVER_ROOT")
LOG = required_env("GSTEST_URI_RESOLVER_LOG")


def safe_error_text(exc):
    try:
        return str(exc)
    except Exception:
        return " ".join(repr(a) for a in exc.args)


def store_unbundled(name):
    """A copy of the unbundled fixture at `gstest:<name>/`; returns the
    meta.json identifier."""
    target = STORE / name
    target.mkdir(parents=True, exist_ok=False)
    for source in SOURCE.iterdir():
        shutil.copyfile(source, target / source.name)
    return f"gstest:{name}/meta.json"


def reset_log():
    LOG.write_text("", encoding="utf-8")


def log_lines():
    if not LOG.exists():
        return []
    return [line for line in LOG.read_text(encoding="utf-8").splitlines()
            if line]


def opened():
    return {line[len("open "):] for line in log_lines()
            if line.startswith("open ")}


def buffered():
    return {line[len("buffer "):] for line in log_lines()
            if line.startswith("buffer ")}


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


# --- rows of the matrix ------------------------------------------------------


def case_filesystem_unaffected(root):
    """Registering a URI resolver leaves the primary resolver in charge of
    every scheme-less path: a filesystem meta.json still opens, and the URI
    resolver is never asked for anything."""
    target = root / "filesystem"
    shutil.copytree(SOURCE, target)
    reset_log()
    stage = Usd.Stage.Open(str((target / "meta.json").resolve()))
    assert stage, "filesystem open failed with the URI resolver registered"
    assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT
    assert not log_lines(), log_lines()


def case_uri_unbundled():
    """A URI-style meta.json is read through ArAsset, and each companion is
    the plugin's `./<plane>` anchored on the URI -- resolved and served by the
    URI resolver, as a stream without GetBuffer()."""
    identifier = store_unbundled("direct")
    reset_log()
    stage = Usd.Stage.Open(identifier)
    assert stage, f"failed to open {identifier}"
    assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT

    companions = {f"gstest:direct/{plane}" for plane in PLANES}
    assert opened() == {identifier} | companions, sorted(opened())
    # The plugin asked every companion for a buffer, got none, and still
    # decoded the plane, so it read the stream through ArAsset::Read().
    assert companions <= buffered(), sorted(buffered())


def case_uri_reference():
    """A filesystem layer references a URI; the companions are anchored on
    the URI the reference resolved to, not on the referencing layer."""
    identifier = store_unbundled("referenced/cloud")
    with tempfile.TemporaryDirectory(prefix="gaussian-sog-uri-ref-") as tmp:
        layer_path = pathlib.Path(tmp) / "shot.usda"
        write_referencing_layer(layer_path, identifier)
        reset_log()
        stage = Usd.Stage.Open(str(layer_path))
        assert stage, "URI reference failed"
        assert gaussian_count(stage, "/Root/Splat") == EXPECTED_COUNT
        del stage
    assert {f"gstest:referenced/cloud/{plane}" for plane in PLANES} \
        <= opened(), sorted(opened())


def case_uri_bundled():
    """A bundled `.sog` behind a URI has no companions: the resolver is asked
    for the archive alone."""
    target = STORE / "bundled"
    target.mkdir(parents=True)
    shutil.copyfile(BUNDLED, target / "cloud.sog")
    identifier = "gstest:bundled/cloud.sog"
    reset_log()
    stage = Usd.Stage.Open(identifier)
    assert stage, f"failed to open {identifier}"
    assert gaussian_count(stage, "/Asset/Splat") == EXPECTED_COUNT
    assert opened() == {identifier}, sorted(opened())


def case_uri_missing_companion():
    """A companion the resolver cannot resolve is GSSOG-E008, naming the
    plane and the URI it was anchored on. A same-named valid plane at the
    store root and in the current directory is never used."""
    identifier = store_unbundled("missing/cloud")
    (STORE / "missing" / "cloud" / "quats.webp").unlink()
    shutil.copyfile(SOURCE / "quats.webp", STORE / "missing" / "quats.webp")
    with tempfile.TemporaryDirectory(prefix="gaussian-sog-uri-cwd-") as tmp:
        shutil.copyfile(SOURCE / "quats.webp",
                        pathlib.Path(tmp) / "quats.webp")
        previous = os.getcwd()
        os.chdir(tmp)
        try:
            reset_log()
            caught = expect_failure(lambda: Usd.Stage.Open(identifier),
                                    "GSSOG-E008")
        finally:
            os.chdir(previous)
    assert "quats.webp" in caught, caught
    assert identifier in caught, caught
    assert "gstest:missing/quats.webp" not in opened(), sorted(opened())


def case_uri_metadata_only():
    """A metadata-only read of a URI parses meta.json alone: with every
    companion gone it still succeeds, and the resolver is never asked for a
    plane."""
    identifier = store_unbundled("metadata-only")
    for plane in PLANES:
        (STORE / "metadata-only" / plane).unlink()
    reset_log()
    layer = Sdf.Layer.OpenAsAnonymous(identifier, metadataOnly=True)
    assert layer, "URI metadata-only open failed without companions"
    gs = layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    assert gs.get("gaussianCount") == EXPECTED_COUNT, gs
    assert gs.get("shDegree") == 3, gs
    assert opened() == {identifier}, sorted(opened())


def main():
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))
    assert Sdf.FileFormat.FindByExtension("sog"), \
        "SOG file format is not registered"
    assert Plug.Registry().GetPluginWithName("gsTestUriResolver"), \
        "the gstest: URI resolver plugin is not registered"

    if STORE.exists():
        shutil.rmtree(STORE)
    STORE.mkdir(parents=True)

    with tempfile.TemporaryDirectory(prefix="gaussian-sog-uri-") as tmp:
        case_filesystem_unaffected(pathlib.Path(tmp))
    case_uri_unbundled()
    case_uri_reference()
    case_uri_bundled()
    case_uri_missing_companion()
    case_uri_metadata_only()

    print("gaussian-sog URI resolver matrix: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
