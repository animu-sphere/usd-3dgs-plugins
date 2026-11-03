#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Define, generate, and measure the large-asset benchmark corpus.

The corpus itself is declared in `tests/benchmark/corpus.json` and documented
in `docs/reference/BENCHMARK_CORPUS.md`. This tool is its only consumer:

    python scripts/benchmark_corpus.py list [--ci | --tier T | --id ID ...]
    python scripts/benchmark_corpus.py verify
    python scripts/benchmark_corpus.py generate --work DIR [selection]
    python scripts/benchmark_corpus.py run [--work DIR] [--assets DIR]
                                           [--report FILE] [selection]

`verify` needs only Python: it checks the manifest's internal consistency,
the committed assets' hashes, and that the fast WebP encoder the scale tier
uses emits the same bytes as the fixture generator's reference encoder.

`generate` writes the `scale` tier: translated copies of the committed CC0
`yashica-t4-top8192.ply` subset on a cubic grid, in all three formats, from
one in-memory source, so the PLY, SPZ, and SOG rows of one step describe the
same Gaussians. Generation is deterministic and checked against the hashes the
manifest records; a mismatch is a failure, not a refresh.

`run` measures the selected assets through `scripts/benchmark_import.py`, one
asset per process so peak resident memory is attributable, and checks each
row against the manifest's expectations (Gaussian count, SH degree) and its
resource targets. `local` and `download` assets are looked up in `--assets`
(or `USD3DGS_BENCHMARK_ASSETS`) and reported as unavailable when absent. Without
`--work`, generated assets live in a temporary directory removed on exit.

`run` needs the same environment as the integration tests: OpenUSD's Python
modules and all three plugins on `PYTHONPATH` / `PXR_PLUGINPATH_NAME`.
Requires only the standard library.
"""

from __future__ import annotations

import argparse
import array
import bisect
import hashlib
import importlib.util
import json
import math
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
import zlib

REPO = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = REPO / "tests" / "benchmark" / "corpus.json"
SCHEMA = "usd-3dgs-plugins.benchmark-corpus/v1"
BENCHMARK_IMPORT = REPO / "scripts" / "benchmark_import.py"
SOG_TOOLS = REPO / "plugins" / "gaussian-sog" / "tools" / "generate_fixtures.py"

TIERS = ("fixture", "corpus", "scale", "real")
FORMATS = ("ply", "spz", "sog")
SOURCE_KINDS = ("committed", "generated", "local", "download")

# The shared declared-count ceiling (docs/reference/IMPORT_LIMITS.md). The
# scale tier's top step sits just under it, so it must never exceed it.
IMPORT_COUNT_LIMIT = 8_000_000

SPZ_MAGIC = 0x5053474E
SPZ_VERSION = 2
SPZ_FRACTIONAL_BITS = 12
SPZ_COLOR_SCALE = 0.15
SPZ_COMPRESSION_LEVEL = 6

ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
CHUNK = 1 << 20


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def load_sog_tools():
    """The SOG fixture generator, imported by path: the scale tier reuses its
    VP8L writer, quaternion packing, and SH sign table instead of copying
    them."""
    spec = importlib.util.spec_from_file_location("_gaussian_sog_tools", SOG_TOOLS)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            digest.update(block)
    return digest.hexdigest()


def spz_payload_sha256(path: pathlib.Path) -> str:
    """SHA-256 of the decompressed SPZ stream. Deflate output is not
    standardized across zlib builds, so the payload, not the container, is
    the reproducibility check (the SPZ corpus README makes the same call)."""
    digest = hashlib.sha256()
    inflater = zlib.decompressobj(wbits=31)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            digest.update(inflater.decompress(block))
    digest.update(inflater.flush())
    return digest.hexdigest()


# --- manifest ----------------------------------------------------------------


def load_manifest() -> dict:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA:
        fail(f"{MANIFEST}: schema is {manifest.get('schema')!r}, expected {SCHEMA!r}")
    return manifest


def select(manifest: dict, args) -> list[dict]:
    assets = manifest["assets"]
    if getattr(args, "id", None):
        by_id = {asset["id"]: asset for asset in assets}
        missing = [name for name in args.id if name not in by_id]
        if missing:
            fail(f"unknown asset id(s): {', '.join(missing)}")
        return [by_id[name] for name in args.id]
    if getattr(args, "ci", False):
        assets = [asset for asset in assets if asset.get("ci")]
    if getattr(args, "tier", None):
        assets = [asset for asset in assets if asset["tier"] in args.tier]
    if getattr(args, "format", None):
        assets = [asset for asset in assets if asset["format"] in args.format]
    return assets


def check_manifest(manifest: dict) -> list[str]:
    problems: list[str] = []
    seen: set[str] = set()
    scale = manifest.get("scale_source", {})
    base_count = scale.get("gaussian_count")
    for asset in manifest["assets"]:
        name = asset.get("id", "<missing id>")
        if name in seen:
            problems.append(f"{name}: duplicate id")
        seen.add(name)
        if asset.get("tier") not in TIERS:
            problems.append(f"{name}: tier {asset.get('tier')!r} not in {TIERS}")
        if asset.get("format") not in FORMATS:
            problems.append(f"{name}: format {asset.get('format')!r} not in {FORMATS}")
        source = asset.get("source", {})
        kind = source.get("kind")
        if kind not in SOURCE_KINDS:
            problems.append(f"{name}: source kind {kind!r} not in {SOURCE_KINDS}")
        count = asset.get("gaussian_count")
        if count is not None and count > IMPORT_COUNT_LIMIT:
            problems.append(f"{name}: {count:,} Gaussians exceeds the "
                            f"{IMPORT_COUNT_LIMIT:,} import ceiling")
        if not asset.get("license"):
            problems.append(f"{name}: no license recorded")
        if asset.get("ci") and kind not in ("committed", "generated"):
            problems.append(f"{name}: a ci asset must be committed or generated")
        if kind == "committed":
            path = REPO / source.get("path", "")
            if not path.is_file():
                problems.append(f"{name}: committed file {source.get('path')!r} is missing")
            elif sha256_file(path) != source.get("sha256"):
                problems.append(f"{name}: {source['path']} does not match its recorded sha256")
        elif kind == "generated":
            tiles = source.get("tiles")
            if asset.get("tier") != "scale":
                problems.append(f"{name}: generated assets belong to the scale tier")
            if not isinstance(tiles, int) or tiles < 1:
                problems.append(f"{name}: generated asset needs a positive tile count")
            elif base_count and count != tiles * base_count:
                problems.append(f"{name}: {tiles} tiles x {base_count} != {count}")
            key = "payload_sha256" if asset["format"] == "spz" else "sha256"
            if not source.get(key):
                problems.append(f"{name}: generated asset records no {key}")
    base = REPO / scale.get("path", "")
    if not base.is_file():
        problems.append(f"scale_source: {scale.get('path')!r} is missing")
    elif sha256_file(base) != scale.get("sha256"):
        problems.append("scale_source: file does not match its recorded sha256")
    return problems


# --- scale tier: the base tile ------------------------------------------------


class BaseTile:
    """A binary little-endian, all-float32 Gaussian PLY held as one array."""

    def __init__(self, path: pathlib.Path) -> None:
        blob = path.read_bytes()
        self.name = path.name
        self.sha256 = hashlib.sha256(blob).hexdigest()
        end = blob.find(b"end_header\n")
        if end < 0:
            fail(f"{path}: no end_header")
        body = end + len(b"end_header\n")
        lines = blob[:body].decode("ascii").splitlines()
        if lines[:2] != ["ply", "format binary_little_endian 1.0"]:
            fail(f"{path}: not a binary little-endian PLY")
        self.properties: list[str] = []
        self.count = -1
        for line in lines[2:]:
            words = line.split()
            if words[0] == "element":
                if words[1] != "vertex":
                    fail(f"{path}: unexpected element {line!r}")
                self.count = int(words[2])
            elif words[0] == "property":
                if words[1] not in ("float", "float32"):
                    fail(f"{path}: non-float property {line!r}")
                self.properties.append(words[2])
        self.stride = len(self.properties)
        self.floats = array.array("f")
        self.floats.frombytes(blob[body:body + 4 * self.stride * self.count])
        if sys.byteorder == "big":
            self.floats.byteswap()
        if len(self.floats) != self.stride * self.count:
            fail(f"{path}: truncated vertex data")
        rest = sum(1 for name in self.properties if name.startswith("f_rest_"))
        self.sh_degree = math.isqrt(rest // 3 + 1) - 1
        self.rest_per_channel = rest // 3

    def column(self, name: str) -> list[float]:
        return self.floats[self.properties.index(name)::self.stride].tolist()

    def rows(self, *names: str) -> list[tuple[float, ...]]:
        return list(zip(*(self.column(name) for name in names)))

    def rest(self) -> list[list[tuple[float, float, float]]]:
        """Per Gaussian, per coefficient: (r, g, b). PLY stores `f_rest_*`
        channel-major: all red coefficients, then green, then blue."""
        k = self.rest_per_channel
        channels = [[self.column(f"f_rest_{c * k + i}") for i in range(k)]
                    for c in range(3)]
        return [[(channels[0][i][g], channels[1][i][g], channels[2][i][g])
                 for i in range(k)] for g in range(self.count)]


def tile_offsets(base: BaseTile, tiles: int) -> list[tuple[float, float, float]]:
    """Grid offsets for `tiles` copies, filled x-fastest on the smallest cube
    that holds them and centered on the origin. The spacing is the base's
    largest extent plus 25 %, rounded up to 1/16 so that every offset is
    exact in float32 and in SPZ's 12-bit fixed point."""
    extents = []
    for axis in ("x", "y", "z"):
        values = base.column(axis)
        extents.append(max(values) - min(values))
    spacing = math.ceil(max(extents) * 1.25 * 16.0) / 16.0
    side = 1
    while side ** 3 < tiles:
        side += 1
    center = (side - 1) / 2.0
    offsets = []
    for index in range(tiles):
        ix, iy, iz = index % side, (index // side) % side, index // (side * side)
        offsets.append(((ix - center) * spacing, (iy - center) * spacing,
                        (iz - center) * spacing))
    return offsets


def sigmoid(value: float) -> float:
    return 1.0 / (1.0 + math.exp(-value))


# --- scale tier: PLY ------------------------------------------------------------


def write_ply(base: BaseTile, offsets, path: pathlib.Path) -> dict:
    """The base rows with x/y/z translated per tile, in the base's own
    property order (Brush's lexicographic order, no normals)."""
    total = base.count * len(offsets)
    header = "\n".join(
        ["ply", "format binary_little_endian 1.0",
         "comment Generated by scripts/benchmark_corpus.py (scale tier)",
         f"comment {len(offsets)} translated copies of {base.name}",
         f"comment source sha256 {base.sha256}",
         f"element vertex {total}"]
        + [f"property float {name}" for name in base.properties]
        + ["end_header", ""]).encode("ascii")
    columns = [base.properties.index(axis) for axis in ("x", "y", "z")]
    axes = [base.column(axis) for axis in ("x", "y", "z")]
    digest = hashlib.sha256(header)
    with path.open("wb") as stream:
        stream.write(header)
        for offset in offsets:
            tile = array.array("f", base.floats)
            for column, values, delta in zip(columns, axes, offset):
                tile[column::base.stride] = array.array("f", [v + delta for v in values])
            if sys.byteorder == "big":
                tile.byteswap()
            data = tile.tobytes()
            digest.update(data)
            stream.write(data)
    return {"sha256": digest.hexdigest(), "bytes": path.stat().st_size}


# --- scale tier: SPZ ------------------------------------------------------------


def _uint8(value: float) -> int:
    return max(0, min(255, int(round(value))))


def write_spz(base: BaseTile, offsets, path: pathlib.Path, sh_flip) -> dict:
    """SPZ v2 through the reference serializer's quantization (the formulas
    `plugins/gaussian-spz/tools/generate_fixtures.py` documents), after the
    PLY-native RDF -> SPZ-native RUB conversion of ADR 0001: negate y and z
    on positions and on the quaternion vector part, and apply the SH sign
    table. Only positions change per tile; every other attribute stream is
    the base's, repeated."""
    count = base.count
    total = count * len(offsets)
    scale = 1 << SPZ_FRACTIONAL_BITS
    xs, ys, zs = (base.column(axis) for axis in ("x", "y", "z"))
    fixed = []
    for x, y, z in zip(xs, ys, zs):
        fixed.extend((round(x * scale), round(-y * scale), round(-z * scale)))
    limit = 1 << 23

    alphas = bytes(_uint8(sigmoid(o) * 255.0) for o in base.column("opacity"))
    colors = bytes(_uint8(c * (SPZ_COLOR_SCALE * 255.0) + 127.5)
                   for row in base.rows("f_dc_0", "f_dc_1", "f_dc_2") for c in row)
    scales = bytes(_uint8((s + 10.0) * 16.0)
                   for row in base.rows("scale_0", "scale_1", "scale_2") for s in row)
    rotations = bytearray()
    for w, x, y, z in base.rows("rot_0", "rot_1", "rot_2", "rot_3"):
        norm = math.sqrt(w * w + x * x + y * y + z * z)
        w, x, y, z = w / norm, x / norm, -y / norm, -z / norm
        sign = -127.5 if w < 0 else 127.5
        rotations += bytes((_uint8(x * sign + 127.5), _uint8(y * sign + 127.5),
                            _uint8(z * sign + 127.5)))
    sh = bytearray()
    for coefficients in base.rest():
        for index, rgb in enumerate(coefficients):
            sh += bytes(max(0, min(255, int(round(v * sh_flip[index] * 128.0)) + 128))
                        for v in rgb)

    header = struct.pack("<IIIBBBB", SPZ_MAGIC, SPZ_VERSION, total, base.sh_degree,
                         SPZ_FRACTIONAL_BITS, 0, 0)
    payload_digest = hashlib.sha256()
    crc = 0
    size = 0
    compressor = zlib.compressobj(SPZ_COMPRESSION_LEVEL, zlib.DEFLATED, -15)
    with path.open("wb") as stream:
        # A fixed gzip header (no name, mtime 0, OS 255) keeps the container
        # independent of the host; zlib's own gzip wrapper stamps the OS.
        stream.write(bytes([0x1F, 0x8B, 0x08, 0, 0, 0, 0, 0, 0x00, 0xFF]))

        def emit(data: bytes) -> None:
            nonlocal crc, size
            payload_digest.update(data)
            crc = zlib.crc32(data, crc)
            size += len(data)
            stream.write(compressor.compress(data))

        emit(header)
        for dx, dy, dz in offsets:
            shift = (round(dx * scale), round(-dy * scale), round(-dz * scale))
            values = [value + shift[i % 3] for i, value in enumerate(fixed)]
            if max(values) >= limit or min(values) < -limit:
                fail("scale tier positions exceed SPZ's 24-bit fixed-point range")
            words = array.array("i", values)
            if sys.byteorder == "big":
                words.byteswap()
            raw = words.tobytes()
            packed = bytearray(3 * len(values))
            packed[0::3] = raw[0::4]
            packed[1::3] = raw[1::4]
            packed[2::3] = raw[2::4]
            emit(bytes(packed))
        for stream_bytes in (alphas, colors, scales, bytes(rotations), bytes(sh)):
            for _ in offsets:
                emit(stream_bytes)
        stream.write(compressor.flush())
        stream.write(struct.pack("<II", crc & 0xFFFFFFFF, size & 0xFFFFFFFF))
    return {"payload_sha256": payload_digest.hexdigest(), "bytes": path.stat().st_size}


# --- scale tier: SOG ------------------------------------------------------------


def quantile_codebook(sog, values: list[float]):
    """256 entries at the midpoints of 256 equal-population bins, ascending,
    and a nearest-entry lookup. Real exports fit k-means; a quantile codebook
    is deterministic and spends its entries where the values are."""
    ordered = sorted(sog.f32(v) for v in values)
    n = len(ordered)
    codebook = [ordered[min(n - 1, (2 * i + 1) * n // 512)] for i in range(256)]
    bounds = [(a + b) / 2.0 for a, b in zip(codebook, codebook[1:])]

    def lookup(value: float) -> int:
        return bisect.bisect_left(bounds, value)

    return codebook, lookup


def write_sog(base: BaseTile, offsets, path: pathlib.Path, sog) -> dict:
    """A bundled SOG v2 archive through the SOG fixture generator's encoding
    rules (SOG_MAPPING.md). SOG stores PLY's native RDF frame, so the base
    rows need no conversion. Positions are re-quantized over the whole grid;
    every other plane is the base's texels, repeated, and the shN palette
    holds one centroid per base Gaussian."""
    count = base.count
    tiles = len(offsets)
    total = count * tiles
    width, height = sog.plane_shape(total)
    padding = width * height - total

    def log_transform(value: float) -> float:
        return math.log(value + 1.0) if value >= 0.0 else -math.log(1.0 - value)

    axes = [base.column(axis) for axis in ("x", "y", "z")]
    mins, maxs, factors = [], [], []
    for axis, values in enumerate(axes):
        low = min(values) + min(offset[axis] for offset in offsets)
        high = max(values) + max(offset[axis] for offset in offsets)
        mins.append(sog.f32(log_transform(low)))
        maxs.append(sog.f32(log_transform(high)))
        span = maxs[-1] - mins[-1]
        factors.append(0.0 if span == 0.0 else 65535.0 / span)

    low_planes = [bytearray() for _ in range(3)]
    high_planes = [bytearray() for _ in range(3)]
    for offset in offsets:
        for axis in range(3):
            delta, floor, factor = offset[axis], mins[axis], factors[axis]
            codes = array.array("H", [
                min(65535, max(0, round((log_transform(v + delta) - floor) * factor)))
                for v in axes[axis]])
            if sys.byteorder == "big":
                codes.byteswap()
            raw = codes.tobytes()
            low_planes[axis] += raw[0::2]
            high_planes[axis] += raw[1::2]

    def per_gaussian(channel: bytes, fill: int) -> bytes:
        return channel * tiles + bytes([fill]) * padding

    def plane(red: bytes, green: bytes, blue: bytes, alpha: bytes) -> bytes:
        return sog.webp_lossless_planar(width, height, red, green, blue, alpha)

    opaque = bytes([255]) * (width * height)
    zeros = bytes(padding)
    planes = {
        "means_l.webp": plane(*(bytes(p) + zeros for p in low_planes), opaque),
        "means_u.webp": plane(*(bytes(p) + zeros for p in high_planes), opaque),
    }
    del low_planes, high_planes

    scale_rows = base.rows("scale_0", "scale_1", "scale_2")
    scale_codebook, scale_index = quantile_codebook(
        sog, [v for row in scale_rows for v in row])
    scale_channels = [bytes(scale_index(row[c]) for row in scale_rows) for c in range(3)]
    planes["scales.webp"] = plane(*(per_gaussian(ch, 0) for ch in scale_channels), opaque)

    quats = [sog.encode_quaternion(row)
             for row in base.rows("rot_0", "rot_1", "rot_2", "rot_3")]
    planes["quats.webp"] = plane(*(per_gaussian(bytes(q[c] for q in quats), 0 if c < 3 else 255)
                                   for c in range(4)))

    dc_rows = base.rows("f_dc_0", "f_dc_1", "f_dc_2")
    sh0_codebook, sh0_index = quantile_codebook(sog, [v for row in dc_rows for v in row])
    sh0_channels = [bytes(sh0_index(row[c]) for row in dc_rows) for c in range(3)]
    sh0_alpha = bytes(_uint8(sigmoid(o) * 255.0) for o in base.column("opacity"))
    planes["sh0.webp"] = plane(*(per_gaussian(ch, 0) for ch in sh0_channels),
                               per_gaussian(sh0_alpha, 255))

    meta = {
        "version": 2,
        "count": total,
        "means": {"mins": mins, "maxs": maxs, "files": ["means_l.webp", "means_u.webp"]},
        "scales": {"codebook": scale_codebook, "files": ["scales.webp"]},
        "quats": {"files": ["quats.webp"]},
        "sh0": {"codebook": sh0_codebook, "files": ["sh0.webp"]},
    }

    if base.sh_degree:
        rest = base.rest()
        coefficients = base.rest_per_channel
        sh_codebook, sh_index = quantile_codebook(
            sog, [v for gaussian in rest for rgb in gaussian for v in rgb])
        centroid_width = 64 * coefficients
        centroid_height = (count + 63) // 64
        texels = centroid_width * centroid_height
        channels = [bytearray(texels) for _ in range(3)]
        for index, gaussian in enumerate(rest):
            start = (index // 64) * centroid_width + (index % 64) * coefficients
            for coefficient, rgb in enumerate(gaussian):
                for c in range(3):
                    channels[c][start + coefficient] = sh_index(rgb[c])
        planes["shN_centroids.webp"] = sog.webp_lossless_planar(
            centroid_width, centroid_height, *(bytes(ch) for ch in channels),
            bytes([255]) * texels)
        labels = array.array("H", range(count))
        if sys.byteorder == "big":
            labels.byteswap()
        raw = labels.tobytes()
        planes["shN_labels.webp"] = plane(
            per_gaussian(raw[0::2], 0), per_gaussian(raw[1::2], 0),
            bytes(width * height), opaque)
        meta["shN"] = {
            "count": count,
            "bands": base.sh_degree,
            "codebook": sh_codebook,
            "files": ["shN_centroids.webp", "shN_labels.webp"],
        }

    with zipfile.ZipFile(path, "w") as archive:
        for entry, payload in [("meta.json", sog.meta_bytes(meta)), *planes.items()]:
            info = zipfile.ZipInfo(entry, date_time=ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o644 << 16
            info.create_system = 0
            archive.writestr(info, payload)
    return {"sha256": sha256_file(path), "bytes": path.stat().st_size}


# --- generation ----------------------------------------------------------------


def generated_matches(asset: dict, path: pathlib.Path) -> bool:
    source = asset["source"]
    if not path.is_file():
        return False
    if asset["format"] == "spz":
        return spz_payload_sha256(path) == source["payload_sha256"]
    return sha256_file(path) == source["sha256"]


def generate(manifest: dict, assets: list[dict], work: pathlib.Path,
             record: bool = False) -> list[str]:
    """Write every selected generated asset into `work`. An existing file
    whose hash already matches is kept. Returns the problems found."""
    generated = [asset for asset in assets if asset["source"]["kind"] == "generated"]
    if not generated:
        return []
    work.mkdir(parents=True, exist_ok=True)
    scale = manifest["scale_source"]
    base = BaseTile(REPO / scale["path"])
    if base.sha256 != scale["sha256"] or base.count != scale["gaussian_count"]:
        return ["scale_source: the base tile does not match the manifest"]
    sog = load_sog_tools()
    problems: list[str] = []
    for asset in generated:
        source = asset["source"]
        path = work / source["file"]
        if not record and generated_matches(asset, path):
            print(f"  {asset['id']}: up to date ({path.name})")
            continue
        start = time.perf_counter()
        offsets = tile_offsets(base, source["tiles"])
        if asset["format"] == "ply":
            result = write_ply(base, offsets, path)
        elif asset["format"] == "spz":
            result = write_spz(base, offsets, path, sog.SH_FLIP_YZ)
        else:
            result = write_sog(base, offsets, path, sog)
        elapsed = time.perf_counter() - start
        print(f"  {asset['id']}: wrote {path.name} "
              f"({result['bytes'] / 2**20:,.1f} MiB, {elapsed:.1f} s)")
        if record:
            # A gzip container's size depends on the zlib build; only the
            # payload hash is a cross-machine fact worth recording for SPZ.
            if asset["format"] == "spz":
                result.pop("bytes")
            source.update(result)
            continue
        key = "payload_sha256" if asset["format"] == "spz" else "sha256"
        if result[key] != source.get(key):
            problems.append(f"{asset['id']}: generated {key} {result[key]} does not "
                            f"match the manifest's {source.get(key)}; generation is "
                            "not reproducing the recorded bytes")
    return problems


# --- measurement ----------------------------------------------------------------


def locate(asset: dict, work: pathlib.Path | None, assets_dir: pathlib.Path | None):
    source = asset["source"]
    kind = source["kind"]
    if kind == "committed":
        return REPO / source["path"]
    if kind == "generated":
        return work / source["file"] if work else None
    if assets_dir is None:
        return None
    path = assets_dir / source["file"]
    return path if path.is_file() else None


def measure(path: pathlib.Path, timeout: float) -> dict:
    completed = subprocess.run(
        [sys.executable, str(BENCHMARK_IMPORT), str(path), "--json"],
        capture_output=True, text=True, timeout=timeout)
    if completed.returncode != 0:
        raise RuntimeError(
            f"benchmark_import.py exited {completed.returncode}:\n"
            f"{completed.stdout}{completed.stderr}")
    return json.loads(completed.stdout)


def evaluate(asset: dict, row: dict, targets: dict) -> list[str]:
    problems = []
    expected = asset["gaussian_count"]
    if expected is None:
        # A real asset whose count is not yet recorded: measure, don't judge.
        expected = row.get("gaussian_count", 0)
    elif row.get("gaussian_count") != expected:
        problems.append(f"Gaussian count {row.get('gaussian_count')} != "
                        f"{asset['gaussian_count']}")
    if row.get("sh_degree") != asset["sh_degree"]:
        problems.append(f"SH degree {row.get('sh_degree')} != {asset['sh_degree']}")
    limit = targets["metadata_only_seconds_max"]
    if row.get("metadata_only_seconds", 0.0) > limit:
        problems.append(f"metadata-only read {row['metadata_only_seconds']:.3f} s "
                        f"> {limit} s: the read is not header-only")
    limit = targets["can_read_seconds_max"]
    if row.get("can_read_seconds", 0.0) > limit:
        problems.append(f"CanRead {row['can_read_seconds']:.3f} s > {limit} s")
    budget = targets["peak_resident_gib"]
    millions = expected / 1e6
    allowed = budget["base"] + budget["per_million_gaussians"] * millions
    peak = row.get("peak_resident_bytes")
    if peak is not None and peak / 2**30 > allowed:
        problems.append(f"peak resident {peak / 2**30:.2f} GiB > budget "
                        f"{allowed:.2f} GiB ({budget['base']} + "
                        f"{budget['per_million_gaussians']} x {millions:.2f} M)")
    return problems


def print_table(rows: list[dict]) -> None:
    print()
    print("| Asset | Format | Gaussians | Source | CanRead | Metadata-only | "
          "Stage.Open | s / M | Flatten | USDC | Peak | GiB / M | Result |")
    print("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: "
          "| ---: | ---: | --- |")
    for row in rows:
        m = row.get("measurement")
        if not m:
            count = row["gaussian_count"]
            print(f"| `{row['id']}` | {row['format']} | {f'{count:,}' if count else '?'} "
                  f"| | | | | | | | | | {row['status']} |")
            continue
        millions = max(m["gaussian_count"], 1) / 1e6
        peak = m.get("peak_resident_bytes")
        # Per-million rates of a small asset only restate process startup.
        per_million = millions >= 0.1
        open_rate = (f"{m['stage_open_seconds'] / millions:.2f}"
                     if per_million else "—")
        peak_rate = (f"{(peak or 0) / 2**30 / millions:.2f}"
                     if per_million and peak else "—")
        print(f"| `{row['id']}` | {row['format']} | {m['gaussian_count']:,} "
              f"| {m['source_bytes'] / 2**20:,.1f} MiB "
              f"| {m.get('can_read_seconds', float('nan')) * 1e3:.1f} ms "
              f"| {m['metadata_only_seconds'] * 1e3:.1f} ms "
              f"| {m['stage_open_seconds']:.2f} s "
              f"| {open_rate} "
              f"| {m['flatten_export_seconds']:.2f} s "
              f"| {m['usdc_bytes'] / 2**20:,.1f} MiB "
              f"| {(peak or 0) / 2**30:.2f} GiB "
              f"| {peak_rate} "
              f"| {row['status']} |")
    print()


def generate_in_child(asset: dict, work: pathlib.Path) -> list[str]:
    """Generate in a separate process. On Linux a child's `ru_maxrss` starts
    from its parent's resident size at fork and survives exec, so generating
    in this process would inflate every later measurement's peak by what the
    generator left resident. The parent stays a few tens of MiB instead."""
    if asset["source"]["kind"] != "generated":
        return []
    completed = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).resolve()), "generate",
         "--id", asset["id"], "--work", str(work)],
        capture_output=True, text=True)
    print(completed.stdout, end="")
    if completed.returncode == 0:
        return []
    return [line.removeprefix("FAIL ") for line in completed.stdout.splitlines()
            if line.startswith("FAIL ")] or [
        f"{asset['id']}: generation exited {completed.returncode}: "
        f"{completed.stderr.strip()}"]


def run(manifest: dict, assets: list[dict], args) -> int:
    assets_dir = args.assets or (pathlib.Path(os.environ["USD3DGS_BENCHMARK_ASSETS"])
                                 if os.environ.get("USD3DGS_BENCHMARK_ASSETS") else None)
    with tempfile.TemporaryDirectory(prefix="usd3dgs-benchmark-") as scratch:
        work = args.work or pathlib.Path(scratch)
        rows = []
        failures = 0
        for asset in assets:
            row = {"id": asset["id"], "tier": asset["tier"], "format": asset["format"],
                   "gaussian_count": asset.get("gaussian_count")}
            # Generate one asset at a time, right before it is measured, so a
            # run without --work holds at most one generated file on disk.
            problems = generate_in_child(asset, work)
            if problems:
                row["status"] = "error"
                row["problems"] = problems
                failures += 1
                rows.append(row)
                for problem in problems:
                    print(f"FAIL {problem}")
                continue
            path = locate(asset, work, assets_dir)
            if path is None or not path.is_file():
                row["status"] = "unavailable"
                if args.require_all:
                    failures += 1
                rows.append(row)
                continue
            try:
                row["measurement"] = measure(path, args.timeout)
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                row["status"] = "error"
                row["problems"] = [str(error)]
                failures += 1
                rows.append(row)
                print(f"FAIL {asset['id']}: {error}")
                continue
            row["problems"] = evaluate(asset, row["measurement"], manifest["targets"])
            row["status"] = "fail" if row["problems"] else "pass"
            failures += bool(row["problems"])
            for problem in row["problems"]:
                print(f"FAIL {asset['id']}: {problem}")
            rows.append(row)
            if args.work is None and asset["source"]["kind"] == "generated":
                path.unlink()
    print_table(rows)
    report = {
        "schema": "usd-3dgs-plugins.benchmark-report/v1",
        "manifest": str(MANIFEST.relative_to(REPO)),
        "date_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "platform": {"system": sys.platform, "python": sys.version.split()[0],
                     "cpu_count": os.cpu_count()},
        "targets": manifest["targets"],
        "rows": rows,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"report: {args.report}")
    measured = sum(1 for row in rows if row.get("measurement"))
    print(f"{measured} measured, "
          f"{sum(1 for row in rows if row['status'] == 'unavailable')} unavailable, "
          f"{failures} failed")
    return 1 if failures else 0


# --- commands -------------------------------------------------------------------


def command_list(manifest: dict, args) -> int:
    for asset in select(manifest, args):
        count = asset.get("gaussian_count")
        print(f"{asset['id']:<28} {asset['tier']:<8} {asset['format']:<4} "
              f"{(f'{count:,}' if count else '?'):>10}  {asset['source']['kind']:<10}"
              f"{'  ci' if asset.get('ci') else ''}")
    return 0


def command_verify(manifest: dict, args) -> int:
    problems = check_manifest(manifest)
    sog = load_sog_tools()
    # The scale tier's planes go through the planar encoder; the committed
    # fixtures go through the reference one. They must be the same bitstream.
    for width, height in ((1, 1), (3, 2), (7, 5), (64, 3)):
        texels = [((7 * i + 1) % 256, (13 * i + 2) % 256, (29 * i + 3) % 256,
                   (31 * i + 4) % 256) for i in range(width * height)]
        reference = sog.webp_lossless(width, height, texels)
        channels = [bytes(t[c] for t in texels) for c in range(4)]
        if sog.webp_lossless_planar(width, height, *channels) != reference:
            problems.append(f"webp_lossless_planar differs from webp_lossless at "
                            f"{width}x{height}")
    for problem in problems:
        print(f"FAIL {problem}")
    print(f"{len(manifest['assets'])} assets declared, {len(problems)} problem(s)")
    return 1 if problems else 0


def command_generate(manifest: dict, args) -> int:
    assets = select(manifest, args)
    problems = generate(manifest, assets, args.work, record=args.record)
    if args.record:
        MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"recorded generated hashes in {MANIFEST.relative_to(REPO)}")
    for problem in problems:
        print(f"FAIL {problem}")
    return 1 if problems else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Define, generate, and measure the large-asset benchmark corpus.")
    commands = parser.add_subparsers(dest="command", required=True)

    def add_selection(sub) -> None:
        sub.add_argument("--ci", action="store_true",
                         help="only assets the hosted CI gate measures")
        sub.add_argument("--tier", action="append", choices=TIERS)
        sub.add_argument("--format", action="append", choices=FORMATS)
        sub.add_argument("--id", action="append", help="one asset id (repeatable)")

    add_selection(commands.add_parser("list", help="list declared assets"))
    commands.add_parser("verify", help="check the manifest and committed assets")
    sub = commands.add_parser("generate", help="write the generated scale tier")
    add_selection(sub)
    sub.add_argument("--work", type=pathlib.Path, required=True)
    sub.add_argument("--record", action="store_true",
                     help="write the generated hashes into the manifest "
                          "instead of checking them")
    sub = commands.add_parser("run", help="measure assets against the targets")
    add_selection(sub)
    sub.add_argument("--work", type=pathlib.Path,
                     help="keep generated assets here (default: a temporary "
                          "directory, removed on exit)")
    sub.add_argument("--assets", type=pathlib.Path,
                     help="directory holding local and download assets")
    sub.add_argument("--report", type=pathlib.Path, help="write a JSON report")
    sub.add_argument("--timeout", type=float, default=1800.0,
                     help="seconds allowed per asset (default 1800)")
    sub.add_argument("--require-all", action="store_true",
                     help="fail when a selected asset is unavailable")
    args = parser.parse_args()

    manifest = load_manifest()
    if args.command == "list":
        return command_list(manifest, args)
    if args.command == "verify":
        return command_verify(manifest, args)
    if args.command == "generate":
        return command_generate(manifest, args)
    return run(manifest, select(manifest, args), args)


if __name__ == "__main__":
    raise SystemExit(main())
