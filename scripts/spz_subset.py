#!/usr/bin/env python3
"""Derive a small, deterministic subset of a Gaussian Splatting SPZ file.

The tool is the SPZ counterpart of `ply_subset.py`: it exists so that
redistributable corpus assets can be regenerated from their recorded source.
It filters by an optional axis aligned bounding box, keeps the N most opaque
Gaussians (ties broken by original index), preserves the source header fields
and the original point order, and writes a machine-readable provenance record
next to the output.

Selection operates on the *quantized* payload: the retained per-point records
are copied byte-for-byte, so no dequantization error is introduced and the
subset decodes exactly as the corresponding points of the source do. The
`--aabb` filter is evaluated in the SPZ-native RUB frame of the file, not in
the RDF frame the plugin authors into USD.

Container versions 1-3 are supported, matching the reader in
`plugins/gaussian-spz`. Version 4 (ZSTD, per-attribute streams) is not.

Usage:
  python scripts/spz_subset.py SOURCE.spz --top-n 8192 --out SUBSET.spz \
      [--aabb minx,miny,minz,maxx,maxy,maxz]
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

PROVENANCE_SCHEMA = "usd-3dgs-plugins.corpus-provenance/v1"
SPZ_MAGIC = 0x5053474E
HEADER_STRUCT = "<IIIBBBB"
HEADER_BYTES = 16
COMPRESSION_LEVEL = 9


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class GzipMember:
    """A single gzip member, split into framing metadata and payload."""

    def __init__(self, blob: bytes):
        if len(blob) < 18 or blob[:2] != b"\x1f\x8b":
            fail("not a gzip stream (SPZ v1-v3 files are gzip members)")
        method, flags, mtime, self.xfl, self.os_byte = struct.unpack(
            "<BBIBB", blob[2:10])
        if method != 8:
            fail(f"unsupported gzip compression method {method}")
        if flags & 0xE0:
            fail(f"reserved gzip flag bits set: 0x{flags:02x}")

        p = 10
        self.extra = self.name = self.comment = None
        if flags & 0x04:
            (xlen,) = struct.unpack("<H", blob[p:p + 2])
            self.extra = blob[p + 2:p + 2 + xlen]
            p += 2 + xlen
        if flags & 0x08:
            end = blob.index(b"\0", p)
            self.name = blob[p:end]
            p = end + 1
        if flags & 0x10:
            end = blob.index(b"\0", p)
            self.comment = blob[p:end]
            p = end + 1
        if flags & 0x02:
            p += 2
        self.mtime = mtime

        decompressor = zlib.decompressobj(-zlib.MAX_WBITS)
        self.payload = decompressor.decompress(blob[p:])
        self.payload += decompressor.flush()
        if len(decompressor.unused_data) > 8:
            fail("trailing bytes after the gzip member")


def write_gzip(payload: bytes, xfl: int, os_byte: int) -> bytes:
    """Frame `payload` as a single gzip member with deterministic metadata.

    mtime is written as zero and no optional field is emitted, so the framing
    carries nothing about the machine that produced the file. XFL and the OS
    byte are carried over from the source so the container stays faithful to
    the producer that wrote it.
    """
    header = b"\x1f\x8b\x08\x00" + struct.pack("<IBB", 0, xfl, os_byte)
    compressor = zlib.compressobj(COMPRESSION_LEVEL, zlib.DEFLATED,
                                  -zlib.MAX_WBITS)
    deflated = compressor.compress(payload) + compressor.flush()
    trailer = struct.pack("<II", zlib.crc32(payload) & 0xFFFFFFFF,
                          len(payload) & 0xFFFFFFFF)
    return header + deflated + trailer


class SpzHeader:
    def __init__(self, payload: bytes):
        if len(payload) < HEADER_BYTES:
            fail("stream is shorter than the 16-byte SPZ header")
        (self.magic, self.version, self.num_points, self.sh_degree,
         self.fractional_bits, self.flags, self.reserved) = struct.unpack(
            HEADER_STRUCT, payload[:HEADER_BYTES])
        if self.magic != SPZ_MAGIC:
            fail(f"bad SPZ magic 0x{self.magic:08x}")
        if not 1 <= self.version <= 3:
            fail(f"unsupported SPZ version {self.version} (this tool reads 1-3)")
        if self.num_points <= 0:
            fail("SPZ header declares no points")
        if self.sh_degree > 3:
            fail(f"unsupported SH degree {self.sh_degree}")

    @property
    def position_bytes(self) -> int:
        return 6 if self.version == 1 else 9

    @property
    def rotation_bytes(self) -> int:
        return 4 if self.version >= 3 else 3

    @property
    def sh_dim(self) -> int:
        return 3 * ((self.sh_degree + 1) ** 2 - 1)

    def pack(self, num_points: int) -> bytes:
        return struct.pack(HEADER_STRUCT, self.magic, self.version, num_points,
                           self.sh_degree, self.fractional_bits, self.flags,
                           self.reserved)


def decode_fixed24(block: bytes, offset: int, fractional_bits: int) -> float:
    value = (block[offset] | (block[offset + 1] << 8)
             | (block[offset + 2] << 16))
    if value & 0x800000:
        value -= 0x1000000
    return value / float(1 << fractional_bits)


def decode_float16(block: bytes, offset: int) -> float:
    return struct.unpack_from("<e", block, offset)[0]


def parse_aabb(text: str) -> tuple[float, ...]:
    parts = [float(v) for v in text.split(",")]
    if len(parts) != 6:
        fail("--aabb expects minx,miny,minz,maxx,maxy,maxz")
    if parts[0] > parts[3] or parts[1] > parts[4] or parts[2] > parts[5]:
        fail("--aabb min must not exceed max on any axis")
    return tuple(parts)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path, help="SPZ v1-v3 file")
    parser.add_argument("--top-n", type=int, required=True,
                        help="keep the N most opaque Gaussians")
    parser.add_argument("--out", type=Path, required=True, help="output SPZ path")
    parser.add_argument("--aabb", type=parse_aabb, default=None,
                        help="position filter applied before --top-n, in the "
                             "SPZ-native RUB frame: minx,miny,minz,maxx,maxy,maxz")
    args = parser.parse_args()
    if args.top_n <= 0:
        fail("--top-n must be positive")

    blob = args.source.read_bytes()
    member = GzipMember(blob)
    payload = member.payload
    header = SpzHeader(payload)

    n = header.num_points
    widths = {
        "positions": header.position_bytes,
        "alphas": 1,
        "colors": 3,
        "scales": 3,
        "rotations": header.rotation_bytes,
        "sh": header.sh_dim,
    }
    expected = HEADER_BYTES + n * sum(widths.values())
    if len(payload) != expected:
        fail(f"payload is {len(payload)} bytes, expected {expected} for "
             f"{n} points at SH degree {header.sh_degree}")

    # Slice the payload into its per-attribute planes; SPZ stores each
    # attribute contiguously for all points, in this order.
    planes: dict[str, bytes] = {}
    offset = HEADER_BYTES
    for name, width in widths.items():
        planes[name] = payload[offset:offset + n * width]
        offset += n * width

    candidates: list[int] | range = range(n)
    if args.aabb is not None:
        positions = planes["positions"]
        width = header.position_bytes
        x0, y0, z0, x1, y1, z1 = args.aabb
        kept = []
        for i in range(n):
            base = i * width
            if header.version == 1:
                x = decode_float16(positions, base)
                y = decode_float16(positions, base + 2)
                z = decode_float16(positions, base + 4)
            else:
                bits = header.fractional_bits
                x = decode_fixed24(positions, base, bits)
                y = decode_fixed24(positions, base + 3, bits)
                z = decode_fixed24(positions, base + 6, bits)
            if x0 <= x <= x1 and y0 <= y <= y1 and z0 <= z <= z1:
                kept.append(i)
        candidates = kept
        if not candidates:
            fail("no Gaussians inside --aabb")

    # Alpha is a single unsigned byte per point; ranking on the quantized
    # value avoids introducing a dequantization step into the selection.
    alphas = planes["alphas"]
    ranked = sorted(candidates, key=lambda i: (-alphas[i], i))
    selected = sorted(ranked[: args.top_n])

    out_payload = bytearray(header.pack(len(selected)))
    for name, width in widths.items():
        plane = planes[name]
        for i in selected:
            out_payload += plane[i * width:(i + 1) * width]
    out_payload = bytes(out_payload)
    out_blob = write_gzip(out_payload, member.xfl, member.os_byte)
    args.out.write_bytes(out_blob)

    provenance = {
        "schema": PROVENANCE_SCHEMA,
        "source": {
            "file": args.source.name,
            "sha256": sha256_bytes(blob),
            "bytes": len(blob),
            "payload_sha256": sha256_bytes(payload),
            "payload_bytes": len(payload),
            "gaussian_count": n,
            "sh_degree": header.sh_degree,
            "spz_version": header.version,
            "fractional_bits": header.fractional_bits,
            "spz_flags": header.flags,
        },
        "derivation": {
            "tool": "scripts/spz_subset.py",
            "selection": "aabb filter in the SPZ-native RUB frame, then top-N "
                         "quantized alpha, original point order preserved",
            "top_n": args.top_n,
            "aabb": list(args.aabb) if args.aabb is not None else None,
            "candidates_after_aabb": len(candidates),
            "compression_level": COMPRESSION_LEVEL,
            "date_utc": datetime.datetime.now(datetime.timezone.utc)
                        .strftime("%Y-%m-%d"),
        },
        "output": {
            "file": args.out.name,
            "sha256": sha256_bytes(out_blob),
            "bytes": len(out_blob),
            # The deflate byte stream depends on the zlib build, so the
            # decompressed payload hash is the regeneration check that holds
            # across machines; the container hash pins this specific write.
            "payload_sha256": sha256_bytes(out_payload),
            "payload_bytes": len(out_payload),
            "gaussian_count": len(selected),
            "sh_degree": header.sh_degree,
            "spz_version": header.version,
        },
    }
    provenance_path = args.out.with_suffix(args.out.suffix + ".provenance.json")
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n",
                               encoding="ascii", newline="\n")

    print(f"{args.out}: {len(selected)} of {n} gaussians, {len(out_blob)} bytes "
          f"({len(out_payload)} decompressed), SPZ v{header.version}, "
          f"SH degree {header.sh_degree}")
    print(f"{provenance_path}: written")


if __name__ == "__main__":
    main()
