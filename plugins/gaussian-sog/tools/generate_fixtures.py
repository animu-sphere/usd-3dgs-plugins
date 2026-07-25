# SPDX-License-Identifier: Apache-2.0
"""Generate the deterministic SOG v2 fixtures used by the reader, decoder, and
plugin tests.

Everything here is written by hand, with no third-party dependency:

* the **lossless WebP (VP8L) property planes**, emitted by the minimal encoder
  below. It writes the one bitstream shape these fixtures need — no transform,
  no color cache, and a flat 8-bit Huffman code per channel, so every texel is
  four literal bytes. Vendoring libwebp's *encoder* to build test data would
  have doubled the vendored surface for no coverage; a hand-written bitstream
  also keeps the fixtures byte-reproducible from source on every platform.
* the **bundled `.sog` archives**, written through `zipfile` with a fixed
  timestamp so the bytes are reproducible.
* the **quantization**, applied through the exact equations of the reference
  encoder (`https://github.com/playcanvas/splat-transform`,
  `src/lib/writers/write-sog.ts`) as recorded in `docs/reference/SOG_MAPPING.md`.

The positive fixtures encode the decoder test kit's canonical clouds
(`openstrata/gs/testing/DecoderTestKit.h`) so the C++ decoder test can require
`CompareClouds` to be empty against `MakeCanonicalOneGaussianCloud()` and
`MakeCanonicalMultiGaussianCloud()` — the round-trip SOG_FORMAT.md §5 asks for.
The kit's values are mirrored in `KIT_ONE` and `KIT_MULTI` below; if the kit
changes, that test fails loudly rather than silently comparing stale data.

The clouds here are stated in *model* terms (RUB frame, linear scales, opacity
in [0, 1], scalar-first quaternions, Gaussian-major RGB rest coefficients) and
converted to SOG's PLY-native RDF frame on the way out, so the fixtures
exercise the decoder's frame conversion rather than assuming it away.

Run: python tools/generate_fixtures.py
"""

from __future__ import annotations

import json
import math
import pathlib
import struct
import zipfile

ROOT = pathlib.Path(__file__).parents[1] / "tests" / "fixtures"

SQRT1_2 = 0.70710678118654752440
# Sign a Y/Z axis flip induces on each rest SH coefficient, mirroring
# gaussianCore's kShFlipYZ (ADR 0001). Indexed by rest coefficient 0-14.
SH_FLIP_YZ = (
    -1.0, -1.0, +1.0,
    -1.0, +1.0, +1.0, -1.0, +1.0,
    -1.0, +1.0, -1.0, -1.0, +1.0, -1.0, +1.0,
)


def f32(value: float) -> float:
    """Snap to float32, so what meta.json spells is what the decoder loads."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


# --- minimal lossless WebP writer -------------------------------------------


class BitWriter:
    """VP8L bit packing: least-significant bit first, within bytes in order."""

    def __init__(self) -> None:
        self._bytes = bytearray()
        self._accumulator = 0
        self._bits = 0

    def write(self, value: int, count: int) -> None:
        assert 0 <= value < (1 << count), (value, count)
        self._accumulator |= value << self._bits
        self._bits += count
        while self._bits >= 8:
            self._bytes.append(self._accumulator & 0xFF)
            self._accumulator >>= 8
            self._bits -= 8

    def finish(self) -> bytes:
        if self._bits:
            self._bytes.append(self._accumulator & 0xFF)
            self._accumulator = 0
            self._bits = 0
        return bytes(self._bytes)


def _reverse8(value: int) -> int:
    """Huffman codes enter the stream most-significant bit first, which for a
    flat 8-bit code is the symbol with its bits reversed."""
    return int(f"{value:08b}"[::-1], 2)


# Number of code-length symbols _write_flat_code emits: one explicit length 8,
# then 42 six-long repeats plus one three-long repeat = 256 lengths.
_FLAT_CODE_SYMBOLS = 1 + 42 + 1


def _write_flat_code(writer: BitWriter, alphabet_size: int) -> None:
    """A canonical Huffman code giving all 256 byte values the length 8.

    256 codes of length 8 exactly fill the 8-bit code space, so the code is
    complete and every literal costs one byte in the stream. Lengths are
    transmitted with a two-symbol code-length code: symbol 8 (the length) and
    symbol 16 (repeat the previous length 3-6 times).
    """
    writer.write(0, 1)  # not a "simple" code
    # 12 code-length code lengths, in libwebp's kCodeLengthCodeOrder. Position
    # 8 is symbol 16 and position 11 is symbol 8; both get a 1-bit code.
    writer.write(12 - 4, 4)
    for index in range(12):
        writer.write(1 if index in (8, 11) else 0, 3)

    if alphabet_size > 256:
        # The green channel's alphabet also holds the 24 length codes, which
        # this encoder never emits. Bounding the run of transmitted lengths
        # leaves them at zero instead of spelling them out.
        writer.write(1, 1)                      # use an explicit max symbol
        writer.write(2, 3)                      # length_nbits = 2 + 2*2 = 6
        writer.write(_FLAT_CODE_SYMBOLS - 2, 6)
    else:
        writer.write(0, 1)

    writer.write(0, 1)                          # symbol 8: length 8
    for _ in range(42):
        writer.write(1, 1)                      # symbol 16: repeat previous
        writer.write(3, 2)                      # ... 3 + 3 = 6 times
    writer.write(1, 1)
    writer.write(0, 2)                          # ... 3 more, 256 in total


def webp_lossless(width: int, height: int, texels: list[tuple[int, int, int, int]]) -> bytes:
    """One 8-bit RGBA lossless WebP image, `texels` in row-major order."""
    assert len(texels) == width * height, (len(texels), width, height)
    assert 1 <= width <= 16384 and 1 <= height <= 16384

    writer = BitWriter()
    writer.write(0x2F, 8)                       # VP8L signature
    writer.write(width - 1, 14)
    writer.write(height - 1, 14)
    writer.write(1, 1)                          # alpha is used
    writer.write(0, 3)                          # version
    writer.write(0, 1)                          # no transform
    writer.write(0, 1)                          # no color cache
    writer.write(0, 1)                          # no meta Huffman image
    _write_flat_code(writer, 256 + 24)          # green (with the length codes)
    for _ in range(3):
        _write_flat_code(writer, 256)           # red, blue, alpha
    writer.write(1, 1)                          # distance: a "simple" code
    writer.write(0, 1)                          # ... of one symbol
    writer.write(0, 1)                          # ... spelled in one bit
    writer.write(0, 1)                          # ... symbol 0, never emitted
    for red, green, blue, alpha in texels:
        # Literal order in the bitstream: green, red, blue, alpha.
        for channel in (green, red, blue, alpha):
            writer.write(_reverse8(channel), 8)

    payload = writer.finish()
    chunk = b"VP8L" + struct.pack("<I", len(payload)) + payload
    if len(payload) % 2:
        chunk += b"\x00"                        # RIFF chunks are even-sized
    return b"RIFF" + struct.pack("<I", 4 + len(chunk)) + b"WEBP" + chunk


def webp_lossy_header(width: int, height: int) -> bytes:
    """A VP8 (lossy) file whose frame header is well formed enough for
    WebPGetFeatures to report `format = 1`. The reader must reject it as a
    malformed plane before any decoding, so the frame body is never valid."""
    partition = 1
    tag = (partition << 5) | (1 << 4)           # key frame, shown, profile 0
    frame = struct.pack("<I", tag)[:3] + b"\x9d\x01\x2a"
    frame += struct.pack("<HH", width, height)  # 14-bit sizes, scale 0
    frame += b"\x00" * 6                        # padding past the frame header
    chunk = b"VP8 " + struct.pack("<I", len(frame)) + frame
    if len(frame) % 2:
        chunk += b"\x00"
    return b"RIFF" + struct.pack("<I", 4 + len(chunk)) + b"WEBP" + chunk


# --- SOG v2 encoding ---------------------------------------------------------


def log_transform(value: float) -> float:
    return math.copysign(math.log(abs(value) + 1.0), value)


def plane_shape(count: int) -> tuple[int, int]:
    """The reference packs Gaussians row-major into a near-square image; the
    tests rely only on `i = x + y*width`, which a width below the count
    exercises."""
    width = max(1, math.isqrt(count) + (0 if math.isqrt(count) ** 2 == count else 1))
    height = (count + width - 1) // width
    return width, height


def build_codebook(values: list[float]) -> tuple[list[float], dict[float, int]]:
    """A 256-entry codebook holding every value the cloud needs, so the
    fixtures quantize positions and quaternions but carry scales, colors, and
    SH coefficients exactly. Real exports run k-means here."""
    unique = sorted({f32(value) for value in values})
    assert len(unique) <= 256, f"{len(unique)} distinct values exceed a codebook"
    index = {value: position for position, value in enumerate(unique)}
    # Pad deterministically with a ramp that stays inside exp()'s float range.
    codebook = unique + [f32(-8.0 + 0.03 * i) for i in range(256 - len(unique))]
    return codebook, index


class Gaussian:
    """One Gaussian in *model* terms (RUB, linear scale, opacity in [0, 1],
    scalar-first quaternion, Gaussian-major RGB rest coefficients)."""

    def __init__(self, position, scale, rotation, opacity, dc, rest=()):
        self.position = tuple(float(value) for value in position)
        self.scale = tuple(float(value) for value in scale)
        self.rotation = tuple(float(value) for value in rotation)
        self.opacity = float(opacity)
        self.dc = tuple(float(value) for value in dc)
        self.rest = tuple(tuple(float(v) for v in triple) for triple in rest)


def to_sog_frame(gaussian: Gaussian) -> Gaussian:
    """Model RUB -> SOG's PLY-native RDF columns: negate Y and Z on positions
    and the quaternion vector part, and apply the SH sign table (ADR 0001)."""
    x, y, z = gaussian.position
    w, i, j, k = gaussian.rotation
    rest = tuple(
        tuple(component * SH_FLIP_YZ[index] for component in triple)
        for index, triple in enumerate(gaussian.rest))
    return Gaussian((x, -y, -z), gaussian.scale, (w, i, -j, -k),
                    gaussian.opacity, gaussian.dc, rest)


def encode_quaternion(rotation: tuple[float, float, float, float]) -> tuple[int, int, int, int]:
    """Smallest-three: drop the largest component, force it non-negative, and
    store the other three scaled by sqrt(2) (SOG_MAPPING.md §5)."""
    norm = math.sqrt(sum(component * component for component in rotation))
    components = [component / norm for component in rotation]
    largest = max(range(4), key=lambda index: abs(components[index]))
    if components[largest] < 0.0:
        components = [-component for component in components]
    stored = [components[index] for index in range(4) if index != largest]
    quantized = [
        min(255, max(0, round((component / SQRT1_2 + 1.0) / 2.0 * 255.0)))
        for component in stored]
    return (quantized[0], quantized[1], quantized[2], 252 + largest)


def encode_sog(gaussians: list[Gaussian], bands: int = 0) -> tuple[dict, dict[str, bytes]]:
    """The whole SOG v2 encode: `meta.json` plus every property plane."""
    native = [to_sog_frame(gaussian) for gaussian in gaussians]
    count = len(native)
    width, height = plane_shape(count)
    padding = width * height - count

    # Positions: log-domain per-axis range, 16-bit split-precision codes.
    logs = [[log_transform(gaussian.position[axis]) for gaussian in native]
            for axis in range(3)]
    mins = [f32(min(axis_values)) for axis_values in logs]
    maxs = [f32(max(axis_values)) for axis_values in logs]
    low_texels: list[tuple[int, int, int, int]] = []
    high_texels: list[tuple[int, int, int, int]] = []
    for index in range(count):
        codes = []
        for axis in range(3):
            span = maxs[axis] - mins[axis]
            fraction = 0.0 if span == 0.0 else (logs[axis][index] - mins[axis]) / span
            codes.append(min(65535, max(0, round(fraction * 65535.0))))
        low_texels.append((codes[0] & 0xFF, codes[1] & 0xFF, codes[2] & 0xFF, 255))
        high_texels.append((codes[0] >> 8, codes[1] >> 8, codes[2] >> 8, 255))

    # Scales: a log-domain codebook, one index per axis.
    scale_codebook, scale_index = build_codebook(
        [math.log(component) for gaussian in native for component in gaussian.scale])
    scale_texels = [
        tuple([scale_index[f32(math.log(component))] for component in gaussian.scale] + [255])
        for gaussian in native]

    # sh0: a raw-DC codebook in RGB, opacity in alpha.
    sh0_codebook, sh0_index = build_codebook(
        [component for gaussian in native for component in gaussian.dc])
    sh0_texels = [
        tuple([sh0_index[f32(component)] for component in gaussian.dc]
              + [min(255, max(0, round(gaussian.opacity * 255.0)))])
        for gaussian in native]

    quat_texels = [encode_quaternion(gaussian.rotation) for gaussian in native]

    blank = (0, 0, 0, 255)
    planes = {
        "means_l.webp": webp_lossless(width, height, low_texels + [blank] * padding),
        "means_u.webp": webp_lossless(width, height, high_texels + [blank] * padding),
        "scales.webp": webp_lossless(width, height, scale_texels + [blank] * padding),
        "quats.webp": webp_lossless(width, height, quat_texels + [blank] * padding),
        "sh0.webp": webp_lossless(width, height, sh0_texels + [blank] * padding),
    }
    meta = {
        "version": 2,
        "count": count,
        "means": {
            "mins": mins,
            "maxs": maxs,
            "files": ["means_l.webp", "means_u.webp"],
        },
        "scales": {"codebook": scale_codebook, "files": ["scales.webp"]},
        "quats": {"files": ["quats.webp"]},
        "sh0": {"codebook": sh0_codebook, "files": ["sh0.webp"]},
    }

    if bands:
        coefficients = bands * (bands + 2)
        assert all(len(gaussian.rest) == coefficients for gaussian in native)
        sh_codebook, sh_index = build_codebook(
            [component for gaussian in native
             for triple in gaussian.rest for component in triple])
        # One palette centroid per Gaussian, 64 centroids per row.
        centroid_width = 64 * coefficients
        centroid_height = (count + 63) // 64
        centroids = [blank] * (centroid_width * centroid_height)
        for index, gaussian in enumerate(native):
            row = index // 64
            column = (index % 64) * coefficients
            for coefficient, triple in enumerate(gaussian.rest):
                centroids[row * centroid_width + column + coefficient] = tuple(
                    [sh_index[f32(component)] for component in triple] + [255])
        label_texels = [(index & 0xFF, index >> 8, 0, 255) for index in range(count)]
        planes["shN_centroids.webp"] = webp_lossless(
            centroid_width, centroid_height, centroids)
        planes["shN_labels.webp"] = webp_lossless(
            width, height, label_texels + [blank] * padding)
        meta["shN"] = {
            "count": count,
            "bands": bands,
            "codebook": sh_codebook,
            "files": ["shN_centroids.webp", "shN_labels.webp"],
        }
    return meta, planes


# --- fixture writing ---------------------------------------------------------


def meta_bytes(meta: dict) -> bytes:
    return (json.dumps(meta, indent=2, allow_nan=False) + "\n").encode("utf-8")


def write_bundle(
    name: str,
    meta: dict | bytes | None,
    planes: dict[str, bytes],
    compress: bool = False,
    extra: dict[str, bytes] | None = None,
    root: pathlib.Path | None = None,
) -> None:
    """A bundled `.sog`: `meta.json` plus its planes in one ZIP archive.
    `meta=None` omits the entry entirely, which is a ZIP but not a SOG.
    `root` overrides the destination, which lets the cross-format equivalence
    generator reuse this encoder for its own fixture directory."""
    entries: dict[str, bytes] = {}
    if meta is not None:
        entries["meta.json"] = meta if isinstance(meta, bytes) else meta_bytes(meta)
    entries.update(planes)
    if extra:
        entries.update(extra)
    path = (root or ROOT) / name
    path.parent.mkdir(parents=True, exist_ok=True)
    method = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
    with zipfile.ZipFile(path, "w") as archive:
        for entry, payload in entries.items():
            info = zipfile.ZipInfo(entry, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = method
            info.external_attr = 0o644 << 16
            archive.writestr(info, payload)
    print(f"  {name}")


def write_unbundled(directory: str, meta: dict | bytes, planes: dict[str, bytes]) -> None:
    """The unbundled layout: `meta.json` beside its companion planes."""
    target = ROOT / directory
    target.mkdir(parents=True, exist_ok=True)
    (target / "meta.json").write_bytes(
        meta if isinstance(meta, bytes) else meta_bytes(meta))
    for name, payload in planes.items():
        (target / name).write_bytes(payload)
    print(f"  {directory}/meta.json")


# --- the clouds --------------------------------------------------------------

# MakeCanonicalOneGaussianCloud(): one Gaussian, SH degree 0.
KIT_ONE = [Gaussian(
    position=(0.5, -1.25, 2.0),
    scale=(0.01, 0.02, 0.04),
    rotation=(0.70710678, 0.70710678, 0.0, 0.0),
    opacity=0.75,
    dc=(0.25, -0.5, 1.0))]


def kit_multi() -> list[Gaussian]:
    """MakeCanonicalMultiGaussianCloud(): three Gaussians, SH degree 3, with
    every rest coefficient unique across (gaussian, coefficient, channel)."""
    rotations = [
        (1.0, 0.0, 0.0, 0.0),
        (0.70710678, 0.70710678, 0.0, 0.0),
        (0.5, 0.5, 0.5, 0.5),
    ]
    cloud = []
    for index in range(3):
        f = float(index)
        rest = []
        for coefficient in range(15):
            base = f + coefficient / 100.0
            rest.append((base, base + 0.001, base + 0.002))
        cloud.append(Gaussian(
            position=(f + 0.5, -f - 0.25, 2.0 * f - 1.0),
            scale=(0.01 * (f + 1.0), 0.02 * (f + 1.0), 0.05 * (f + 1.0)),
            rotation=rotations[index],
            opacity=0.25 * (f + 1.0),
            dc=(0.1 * (f + 1.0), -0.2 * (f + 1.0), 0.3 * (f + 1.0)),
            rest=rest))
    return cloud


# Two hand-checked Gaussians at SH degree 1: small enough that the C++ test
# spells out every expected model value instead of comparing against the kit.
DEGREE1 = [
    Gaussian(
        position=(1.0, 2.0, -0.5),
        scale=(1.0, math.exp(1.0), math.exp(-1.0)),
        rotation=(1.0, 0.0, 0.0, 0.0),
        opacity=0.8,
        dc=(0.0, 0.5, -0.5),
        rest=[(0.1, 0.2, 0.3), (-0.1, -0.2, -0.3), (0.4, -0.4, 0.5)]),
    Gaussian(
        position=(-3.0, 0.25, 4.0),
        scale=(math.exp(0.5), math.exp(-0.5), 1.0),
        rotation=(0.70710678, 0.70710678, 0.0, 0.0),
        opacity=0.6,
        dc=(0.9, -0.9, 0.0),
        rest=[(0.6, 0.7, 0.8), (-0.6, -0.7, -0.8), (0.9, -0.9, 0.25)]),
]


def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    print("gaussian-sog fixtures:")

    # --- positive: the decoder test kit's canonical clouds, both layouts.
    one_meta, one_planes = encode_sog(KIT_ONE)
    write_bundle("kit-one-degree0.sog", one_meta, one_planes)

    multi_meta, multi_planes = encode_sog(kit_multi(), bands=3)
    write_bundle("kit-multi-degree3.sog", multi_meta, multi_planes)
    # The same cloud through DEFLATE rather than stored entries, so the ZIP
    # inflate path is covered too.
    write_bundle("kit-multi-degree3-deflated.sog", multi_meta, multi_planes,
                 compress=True)
    write_unbundled("unbundled-kit-multi-degree3", multi_meta, multi_planes)

    degree1_meta, degree1_planes = encode_sog(DEGREE1, bands=1)
    write_bundle("decode-degree1.sog", degree1_meta, degree1_planes)

    # A palette label past meta.shN.count: the affected Gaussian decodes with
    # zero higher-order coefficients and the read warns (GSSOG-W001).
    labels_meta = json.loads(json.dumps(degree1_meta))
    labels_meta["shN"]["count"] = 1
    write_bundle("labels-out-of-range.sog", labels_meta, degree1_planes)

    # --- negative: one fixture per container diagnostic.
    (ROOT / "not-sog.sog").write_bytes(b"this is not a container at all\n")
    print("  not-sog.sog")
    write_bundle("no-meta.sog", None, {}, extra={"readme.txt": b"no meta here\n"})
    (ROOT / "bad-zip.sog").write_bytes(b"PK\x03\x04" + b"\x00" * 64)
    print("  bad-zip.sog")

    truncated = meta_bytes(one_meta)[:40]
    write_bundle("malformed-meta.sog", truncated, one_planes)

    legacy = json.loads(json.dumps(one_meta))
    del legacy["version"]
    write_bundle("version-1.sog", legacy, one_planes)
    future = json.loads(json.dumps(one_meta))
    future["version"] = 3
    write_bundle("version-3.sog", future, one_planes)

    empty = json.loads(json.dumps(one_meta))
    empty["count"] = 0
    write_bundle("empty-count.sog", empty, one_planes)

    overflow = json.loads(json.dumps(one_meta))
    overflow["count"] = 4096
    write_bundle("count-exceeds-plane.sog", overflow, one_planes)

    bad_bands = json.loads(json.dumps(multi_meta))
    bad_bands["shN"]["bands"] = 4
    write_bundle("bad-bands.sog", bad_bands, multi_planes)

    short_codebook = json.loads(json.dumps(one_meta))
    short_codebook["scales"]["codebook"] = short_codebook["scales"]["codebook"][:255]
    write_bundle("short-codebook.sog", short_codebook, one_planes)

    escaping = json.loads(json.dumps(one_meta))
    escaping["scales"]["files"] = ["../scales.webp"]
    write_bundle("escaping-plane-name.sog", escaping, one_planes)

    # A plane named for a Windows character device. The name carries no path
    # separator, so only the reserved-name rule refuses it; without that rule
    # the unbundled loader would open the device instead of a file.
    device = json.loads(json.dumps(one_meta))
    device["scales"]["files"] = ["NUL.webp"]
    write_bundle("device-plane-name.sog", device, one_planes)

    # Integral JSON numbers far outside the range of a 64-bit integer. Each one
    # fails its own range check, and the diagnostic must render the value
    # without converting it to an integer type -- that conversion is undefined
    # behaviour for an out-of-range double, not a saturating one.
    huge_version = json.loads(json.dumps(one_meta))
    huge_version["version"] = 1e300
    write_bundle("huge-version.sog", huge_version, one_planes)

    huge_count = json.loads(json.dumps(one_meta))
    huge_count["count"] = 1e300
    write_bundle("huge-count.sog", huge_count, one_planes)

    huge_bands = json.loads(json.dumps(multi_meta))
    huge_bands["shN"]["bands"] = 1e300
    write_bundle("huge-bands.sog", huge_bands, multi_planes)

    huge_palette = json.loads(json.dumps(multi_meta))
    huge_palette["shN"]["count"] = 1e300
    write_bundle("huge-palette-count.sog", huge_palette, multi_planes)

    missing = dict(one_planes)
    del missing["scales.webp"]
    write_bundle("missing-plane.sog", one_meta, missing)

    lossy = dict(one_planes)
    lossy["scales.webp"] = webp_lossy_header(1, 1)
    write_bundle("lossy-plane.sog", one_meta, lossy)

    corrupt = dict(one_planes)
    corrupt["scales.webp"] = one_planes["scales.webp"][:20] + b"\xff" * 12
    write_bundle("corrupt-plane.sog", one_meta, corrupt)

    # A quaternion alpha byte below the 252-255 tag range has no valid reading.
    bad_quats = dict(one_planes)
    bad_quats["quats.webp"] = webp_lossless(1, 1, [(128, 128, 128, 100)])
    write_bundle("bad-quat-tag.sog", one_meta, bad_quats)

    # Routing: valid JSON that is not a SOG meta.json must not be claimed.
    (ROOT / "not-sog.json").write_bytes(
        b'{"version": 2, "kind": "something else"}\n')
    print("  not-sog.json")

    # The unbundled layout with a companion plane absent.
    incomplete = dict(multi_planes)
    del incomplete["sh0.webp"]
    write_unbundled("unbundled-missing-plane", multi_meta, incomplete)


if __name__ == "__main__":
    main()
