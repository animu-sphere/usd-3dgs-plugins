# SPDX-License-Identifier: Apache-2.0
"""Test an aggregate release package in fresh OpenUSD processes."""

import argparse
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile


FIXTURES = (
    ("gaussian-ply", "GaussianPlyFileFormat", "one-gaussian-binary-le.ply", 1, 0),
    ("gaussian-spz", "GaussianSpzFileFormat", "decode-degree1-v2.spz", 2, 1),
    ("gaussian-sog", "GaussianSogFileFormat", "kit-one-degree0.sog", 1, 0),
)


def run(*command, cwd=None):
    subprocess.run(command, cwd=cwd, check=True)


def probe(consumer, prefix, previous):
    search_paths = [
        str(root / "bundles" / bundle / "plugin" / "resources" / bundle)
        for root in previous for bundle, *_ in FIXTURES
    ]
    if search_paths:
        runtime_paths = os.environ.get("PXR_PLUGINPATH_NAME")
        if runtime_paths:
            search_paths.append(runtime_paths)
        os.environ["PXR_PLUGINPATH_NAME"] = os.pathsep.join(search_paths)

    if prefix is not None:
        # The generated activation module also holds the Windows DLL search
        # handles for the lifetime of this Python process.
        runpy.run_path(str(prefix / "openstrata_activate.py"))

    from pxr import Plug, Sdf, Tf, Usd, UsdVol

    for bundle, plugin_name, filename, count, degree in FIXTURES:
        asset = Path(filename).resolve()
        plugin = Plug.Registry().GetPluginWithName(plugin_name)
        extension = asset.suffix[1:]
        if prefix is None:
            assert not plugin, f"{plugin_name} is still discoverable"
            file_format = Sdf.FileFormat.FindByExtension(extension)
            assert not file_format or file_format.formatId != extension, extension
            try:
                stage = Usd.Stage.Open(str(asset))
            except Tf.ErrorException:
                stage = None
            assert not stage, f"{asset.name} opened without its installed plugin"
            continue

        bundle_root = prefix / "bundles" / bundle
        resource_path = bundle_root / "plugin" / "resources" / bundle
        assert plugin, f"{plugin_name} is missing"
        assert Path(plugin.path).resolve().is_relative_to(bundle_root / "lib"), plugin.path
        assert Path(plugin.resourcePath).resolve() == resource_path, plugin.resourcePath

        catalog = json.loads((resource_path / "diagnostics.json").read_text(encoding="utf-8"))
        assert catalog["schema"] == f"{bundle}.diagnostics/v1" and catalog["diagnostics"], bundle

        file_format = Sdf.FileFormat.FindByExtension(extension)
        assert file_format and file_format.formatId == extension, extension
        stage = Usd.Stage.Open(str(asset))
        assert stage and plugin.isLoaded, asset.name
        splat = UsdVol.ParticleField3DGaussianSplat.Get(stage, "/Asset/Splat")
        assert splat and len(splat.GetPositionsAttr().Get()) == count, asset.name
        metadata = stage.GetDefaultPrim().GetCustomDataByKey("gs")
        assert (metadata["gaussianCount"], metadata["shDegree"]) == (count, degree), asset.name

        run(consumer, asset, str(count))
        output = asset.with_suffix(".usda")
        run("usdcat", "--flatten", "--out", output, asset)
        # Run the USD validator on the authored stage. A bundled .sog is
        # itself a ZIP, so usdchecker would apply USDZ archive rules to it.
        run("usdchecker", output)
        print(f"Passed {prefix.name}: {asset.name}", flush=True)


def check_lifecycle(archive, digest):
    test_source = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="usd3dgs-") as temp:
        scratch = Path(temp).resolve()
        build = scratch / "build"
        run("cmake", "-S", test_source, "-B", build,
            "-DCMAKE_BUILD_TYPE=Release", f"-DPython3_EXECUTABLE={sys.executable}", cwd=scratch)
        run("cmake", "--build", build, "--config", "Release", cwd=scratch)
        consumer = build / "Release" / ("open_stage.exe" if os.name == "nt" else "open_stage")

        first = scratch / "installed"
        replacement = scratch / "replacement"
        command = (sys.executable, "-X", "faulthandler", Path(__file__).resolve(), "probe", consumer)
        for prefix in (first, replacement):
            run("ost", "plugin", "product", "install", archive, "--prefix", prefix,
                "--expect-digest", digest, cwd=scratch)
            for bundle, _, filename, _, _ in FIXTURES:
                source = prefix / "bundles" / bundle / "tests" / "fixtures" / filename
                shutil.copyfile(source, scratch / filename)
            if prefix == first:
                run(*command, cwd=scratch)
            previous = ("--previous", first) if prefix == replacement else ()
            run(*command, prefix, *previous, cwd=scratch)

        # The replacement was active while the first install still existed.
        # Remove both isolated prefixes, then probe with their paths still set.
        for prefix in (first, replacement):
            assert prefix.resolve().parent == scratch
            shutil.rmtree(prefix)
        run(*command, "--previous", first, "--previous", replacement, cwd=scratch)
        print("Passed uninstallation: none of the three file formats can open", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    lifecycle = commands.add_parser("run", help="check install, replacement, and removal")
    lifecycle.add_argument("package_report", type=Path, help="JSON from ost plugin package --product")
    worker = commands.add_parser("probe")
    worker.add_argument("consumer", type=Path)
    worker.add_argument("prefix", type=Path, nargs="?")
    worker.add_argument("--previous", type=Path, action="append", default=[])
    args = parser.parse_args()
    if args.command == "probe":
        probe(args.consumer, args.prefix, args.previous)
    else:
        report = json.loads(args.package_report.read_text(encoding="utf-8-sig"))
        product = report["data"]["product"]
        check_lifecycle(Path(product["archive"]).resolve(strict=True), product["archive_digest"])


if __name__ == "__main__":
    main()
