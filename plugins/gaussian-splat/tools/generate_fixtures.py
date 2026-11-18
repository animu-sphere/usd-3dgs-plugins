# SPDX-License-Identifier: Apache-2.0
"""Generate deterministic `.splat` fixtures and the CC0 corpus conversions.

A `.splat` file is a headerless run of 32-byte little-endian records
(SPLAT_FORMAT.md §2):

    offset  0  f32 x 3  position, Graphdeco PLY frame (RDF)
    offset 12  f32 x 3  linear scale, exp() of the PLY log-scale
    offset 24  u8  x 3  colour, (0.5 + SH_C0 * f_dc) * 255, clamped
    offset 27  u8       alpha, sigmoid(opacity) * 255, clamped
    offset 28  u8  x 4  rotation (w, x, y, z), q * 128 + 128, clamped

`encode_record` reproduces the reference writer, the antimatter15
`convert.py`: normalize the quaternion, scale, clamp to [0, 255], then
truncate. `rounding="round"` instead reproduces the writers that round to
nearest (the antimatter15 viewer, Spark). The decoder test and the
cross-format equivalence suite use both, so the decoder is checked against
either writer arithmetic (SPLAT_FORMAT.md §3).

`python generate_fixtures.py` writes `tests/fixtures/`. With `--corpus` it also
converts the committed CC0 PLY subsets in `plugins/gaussian-ply/tests/corpus/`
to `tests/corpus/`, keeping the source order (the reference converter sorts
by importance; keeping the order lets the tests compare index by index).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import struct

BUNDLE = pathlib.Path(__file__).parents[1]
REPO = BUNDLE.parents[1]
ROOT = BUNDLE / "tests" / "fixtures"
CORPUS = BUNDLE / "tests" / "corpus"
PLY_CORPUS = REPO / "plugins" / "gaussian-ply" / "tests" / "corpus"

RECORD_BYTES = 32
SH_C0 = 0.28209479177387814


# --------------------------------------------------------------------------
# Record encoding
# --------------------------------------------------------------------------

def to_byte(value: float, rounding: str) -> int:
    """Clamp to [0, 255], then truncate (`astype(uint8)`) or round."""
    clamped = min(255.0, max(0.0, value))
    return int(clamped) if rounding == "truncate" else int(round(clamped))


def encode_record(position, scale, dc, opacity, rotation,
                  rounding: str = "truncate") -> bytes:
    """One record from Graphdeco PLY-frame values: `position` and `rotation`
    (w, x, y, z) in the RDF frame, `scale` linear, `opacity` in [0, 1]."""
    w, x, y, z = rotation
    norm = math.sqrt(w * w + x * x + y * y + z * z)
    quaternion = (w / norm, x / norm, y / norm, z / norm)
    return (struct.pack("<3f", *position)
            + struct.pack("<3f", *scale)
            + bytes(to_byte((0.5 + SH_C0 * c) * 255.0, rounding) for c in dc)
            + bytes([to_byte(opacity * 255.0, rounding)])
            + bytes(to_byte(q * 128.0 + 128.0, rounding) for q in quaternion))


def raw_record(position, scale, rgba, rotation_bytes) -> bytes:
    """One record from exact stored values, for hand-checked fixtures."""
    return (struct.pack("<3f", *position) + struct.pack("<3f", *scale)
            + bytes(rgba) + bytes(rotation_bytes))


def write(name: str, data: bytes) -> None:
    (ROOT / name).write_bytes(data)


# --------------------------------------------------------------------------
# Fixtures
# --------------------------------------------------------------------------

# A record the decoder accepts, used to pad the negative fixtures to whole
# records so each fails for exactly one reason.
VALID = raw_record((0.0, 0.0, 0.0), (0.1, 0.1, 0.1), (128, 128, 128, 255),
                   (255, 128, 128, 128))


def valid_fixtures() -> None:
    # Two hand-checked records (test_gaussian_splat_decoder.cpp,
    # TestKnownValues). Stored in the Graphdeco RDF frame; the decoder flips
    # Y and Z into the model's RUB frame.
    write("decode-known.splat",
          raw_record((1.0, 2.0, -0.5), (0.5, 1.0, 2.0), (128, 64, 200, 204),
                     # identity: w = 1 clamps to 255
                     (255, 128, 128, 128))
          + raw_record((-3.0, 0.25, 4.0), (0.01, 0.02, 0.04),
                       (0, 255, 128, 153),
                       # 90 degrees about +Y, truncated: 0.7071 -> 218
                       (218, 128, 218, 128)))

    # SplatTransform's own reference fixture is kept in tests/reference; this
    # is a one-record file for the package smoke level.
    write("minimal-one.splat", VALID)

    # Every rotation byte 128: a zero quaternion, replaced by identity with
    # GSSPLAT-W001.
    write("zero-rotation.splat",
          raw_record((0.5, -0.5, 1.5), (0.2, 0.3, 0.4), (10, 20, 30, 40),
                     (128, 128, 128, 128)) + VALID)


def negative_fixtures() -> None:
    write("empty.splat", b"")
    # One whole record and one stray byte.
    write("partial-record.splat", VALID + b"\x00")

    # Files of other formats with a .splat extension, padded to whole
    # records so only the signature can reject them.
    def renamed(prefix: bytes, records: int = 2) -> bytes:
        return prefix + bytes(records * RECORD_BYTES - len(prefix))

    write("renamed-ply.splat", renamed(
        b"ply\nformat binary_little_endian 1.0\nelement vertex 0\n"
        b"end_header\n", records=3))
    write("renamed-ply-crlf.splat", renamed(b"ply\r\nformat ascii 1.0\r\n"))
    write("renamed-spz-v4.splat", renamed(b"NGSP\x04\x00\x00\x00"))
    write("renamed-sog.splat", renamed(b"PK\x03\x04\x14\x00"))
    write("renamed-glb.splat", renamed(b"glTF\x02\x00\x00\x00"))
    # A renamed file whose size is not whole records is still named as the
    # other format, not as a damaged .splat file.
    write("renamed-ply-odd-size.splat", b"ply\nformat ascii 1.0\n")

    nan = float("nan")
    inf = float("inf")
    write("nonfinite-position.splat", VALID + raw_record(
        (0.0, nan, 0.0), (0.1, 0.1, 0.1), (1, 2, 3, 4), (255, 128, 128, 128)))
    write("infinite-position.splat", raw_record(
        (inf, 0.0, 0.0), (0.1, 0.1, 0.1), (1, 2, 3, 4), (255, 128, 128, 128)))
    write("zero-scale.splat", VALID + raw_record(
        (0.0, 0.0, 0.0), (0.1, 0.0, 0.1), (1, 2, 3, 4), (255, 128, 128, 128)))
    write("negative-scale.splat", raw_record(
        (0.0, 0.0, 0.0), (-0.1, 0.1, 0.1), (1, 2, 3, 4), (255, 128, 128, 128)))
    write("nonfinite-scale.splat", raw_record(
        (0.0, 0.0, 0.0), (0.1, 0.1, nan), (1, 2, 3, 4), (255, 128, 128, 128)))


# --------------------------------------------------------------------------
# Corpus conversion
# --------------------------------------------------------------------------

def read_binary_ply(path: pathlib.Path):
    """The vertex rows of a binary little-endian PLY whose vertex properties
    are all float32, as dictionaries. Enough for the committed corpus."""
    data = path.read_bytes()
    end = data.index(b"end_header\n") + len(b"end_header\n")
    lines = data[:end].decode("ascii").splitlines()
    if "format binary_little_endian 1.0" not in lines:
        raise ValueError(f"{path}: not binary little-endian")
    count, names, in_vertex = 0, [], False
    for line in lines:
        words = line.split()
        if words[:2] == ["element", "vertex"]:
            count, in_vertex = int(words[2]), True
        elif words[:1] == ["element"]:
            in_vertex = False
        elif words[:1] == ["property"] and in_vertex:
            if words[1] != "float":
                raise ValueError(f"{path}: property {words[2]} is not float")
            names.append(words[2])
    row = struct.Struct("<" + "f" * len(names))
    return [dict(zip(names, row.unpack_from(data, end + i * row.size)))
            for i in range(count)]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def convert_corpus() -> None:
    for scene in ("yashica-t4", "leica-sofort"):
        source = PLY_CORPUS / scene / f"{scene}-top8192.ply"
        rows = read_binary_ply(source)
        out = b"".join(
            encode_record(
                (r["x"], r["y"], r["z"]),
                tuple(math.exp(r[f"scale_{i}"]) for i in range(3)),
                tuple(r[f"f_dc_{i}"] for i in range(3)),
                1.0 / (1.0 + math.exp(-r["opacity"])),
                tuple(r[f"rot_{i}"] for i in range(4)))
            for r in rows)
        target = CORPUS / scene / f"{scene}-top8192.splat"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(out)
        source_bytes = source.read_bytes()
        provenance = {
            "schema": "usd-3dgs-plugins.corpus-provenance/v1",
            "source": {
                "file": f"plugins/gaussian-ply/tests/corpus/{scene}/{source.name}",
                "sha256": sha256(source_bytes),
                "bytes": len(source_bytes),
                "gaussian_count": len(rows),
            },
            "derivation": {
                "tool": "plugins/gaussian-splat/tools/generate_fixtures.py --corpus",
                "encoding": "antimatter15 convert.py arithmetic (clamp, then "
                            "truncate) without its importance sort; source "
                            "order preserved; rest SH dropped",
            },
            "output": {
                "file": target.name,
                "sha256": sha256(out),
                "bytes": len(out),
                "gaussian_count": len(rows),
                "sh_degree": 0,
            },
        }
        target.with_suffix(".splat.provenance.json").write_text(
            json.dumps(provenance, indent=2) + "\n", encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--corpus", action="store_true",
                        help="also convert the committed CC0 PLY subsets")
    args = parser.parse_args()
    ROOT.mkdir(parents=True, exist_ok=True)
    valid_fixtures()
    negative_fixtures()
    if args.corpus:
        convert_corpus()


if __name__ == "__main__":
    main()
