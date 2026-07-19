# SPDX-License-Identifier: Apache-2.0
"""Measure the design-policy 12.1 import baselines for one Gaussian PLY.

Run one asset per process: peak resident memory is a process-lifetime
figure, so batching assets in one interpreter would attribute the largest
asset's peak to every later row.

    python benchmark_import.py <asset.ply> [--json]

The environment must expose OpenUSD python modules and the gaussian-ply
plugin (PYTHONPATH / PXR_PLUGINPATH_NAME), exactly like the integration
test. Requires only the standard library.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import pathlib
import sys
import tempfile
import time


def peak_resident_bytes() -> int | None:
    if sys.platform == "win32":
        class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
            _fields_ = [
                ("cb", ctypes.c_uint32),
                ("PageFaultCount", ctypes.c_uint32),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        counters = PROCESS_MEMORY_COUNTERS()
        counters.cb = ctypes.sizeof(counters)
        kernel32 = ctypes.windll.kernel32
        # Without an explicit restype ctypes truncates the 64-bit pseudo
        # handle to a 32-bit int and the call fails with ERROR_INVALID_HANDLE.
        kernel32.GetCurrentProcess.restype = ctypes.c_void_p
        handle = kernel32.GetCurrentProcess()
        # Modern Windows exports this from kernel32; psapi.dll is the legacy
        # location and remains as a fallback shim.
        for probe in (getattr(kernel32, "K32GetProcessMemoryInfo", None),
                      getattr(ctypes.windll.psapi, "GetProcessMemoryInfo", None)):
            if probe and probe(ctypes.c_void_p(handle),
                               ctypes.byref(counters), counters.cb):
                return int(counters.PeakWorkingSetSize)
        return None
    try:
        import resource
        peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        # ru_maxrss is KiB on Linux, bytes on macOS.
        return peak if sys.platform == "darwin" else peak * 1024
    except Exception:
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asset", type=pathlib.Path)
    parser.add_argument("--json", action="store_true",
                        help="emit a JSON object instead of readable text")
    args = parser.parse_args()

    from pxr import Plug, Sdf, Usd

    import os
    plugin_path = os.environ.get("PXR_PLUGINPATH_NAME")
    if plugin_path:
        Plug.Registry().RegisterPlugins(plugin_path.split(os.pathsep))

    asset = args.asset.resolve()
    result: dict[str, object] = {
        "asset": str(asset),
        "source_bytes": asset.stat().st_size,
    }

    file_format = Sdf.FileFormat.FindByExtension("ply")
    assert file_format, "PLY file format is not registered"

    if hasattr(file_format, "CanRead"):
        start = time.perf_counter()
        assert file_format.CanRead(str(asset))
        result["can_read_seconds"] = time.perf_counter() - start

    start = time.perf_counter()
    metadata_layer = Sdf.Layer.OpenAsAnonymous(str(asset), metadataOnly=True)
    result["metadata_only_seconds"] = time.perf_counter() - start
    gs = metadata_layer.GetPrimAtPath("/Asset").customData.get("gs", {})
    result["gaussian_count"] = int(gs.get("gaussianCount", 0))
    result["sh_degree"] = int(gs.get("shDegree", -1))
    del metadata_layer

    start = time.perf_counter()
    stage = Usd.Stage.Open(str(asset))
    result["stage_open_seconds"] = time.perf_counter() - start
    assert stage

    with tempfile.TemporaryDirectory() as tmp:
        usdc = pathlib.Path(tmp) / "flattened.usdc"
        start = time.perf_counter()
        stage.Export(str(usdc))
        result["flatten_export_seconds"] = time.perf_counter() - start
        result["usdc_bytes"] = usdc.stat().st_size

    peak = peak_resident_bytes()
    if peak is not None:
        result["peak_resident_bytes"] = peak

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        count = result["gaussian_count"]
        print(f"{asset.name}: {count:,} Gaussians, "
              f"SH degree {result['sh_degree']}")
        print(f"  source           {result['source_bytes'] / 2**20:8.1f} MiB")
        if "can_read_seconds" in result:
            print(f"  CanRead          {result['can_read_seconds']*1e3:8.1f} ms")
        print(f"  metadata-only    {result['metadata_only_seconds']*1e3:8.1f} ms")
        print(f"  Stage.Open       {result['stage_open_seconds']:8.2f} s")
        print(f"  flatten->usdc    {result['flatten_export_seconds']:8.2f} s")
        print(f"  usdc             {result['usdc_bytes'] / 2**20:8.1f} MiB")
        if peak is not None:
            print(f"  peak resident    {peak / 2**30:8.2f} GiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
