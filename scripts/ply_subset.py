#!/usr/bin/env python3
"""Derive a small, deterministic subset of a binary Gaussian Splatting PLY.

The tool exists so that redistributable corpus assets can be regenerated
byte-for-byte from their recorded source: it filters by an optional axis
aligned bounding box, keeps the N most opaque Gaussians (ties broken by
original index), preserves the source property order and comments, and
writes a machine-readable provenance record next to the output.

Only binary little-endian PLY files whose vertex element consists entirely
of float32 properties are supported — the layout every Gaussian Splatting
trainer observed so far emits. The subset is written in the original vertex
order, so the derivation is deterministic for a given source and arguments.

Usage:
  python scripts/ply_subset.py SOURCE.ply --top-n 8192 --out SUBSET.ply \
      [--aabb minx,miny,minz,maxx,maxy,maxz]
"""

from __future__ import annotations

import argparse
import array
import datetime
import hashlib
import json
import math
import sys
from pathlib import Path

PROVENANCE_SCHEMA = "usd-3dgs-plugins.corpus-provenance/v1"


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class PlyHeader:
    def __init__(self, raw: bytes):
        self.raw = raw
        self.comments: list[str] = []
        self.properties: list[str] = []
        self.vertex_count = -1

        lines = raw.decode("ascii", errors="strict").splitlines()
        if not lines or lines[0] != "ply":
            fail("not a PLY file (missing 'ply' magic)")
        if lines[1] != "format binary_little_endian 1.0":
            fail(f"unsupported format line: {lines[1]!r} "
                 "(only binary_little_endian 1.0 is supported)")

        current_element = None
        for line in lines[2:]:
            if line == "end_header":
                break
            keyword, _, rest = line.partition(" ")
            if keyword == "comment":
                self.comments.append(rest)
            elif keyword == "element":
                name, _, count = rest.partition(" ")
                current_element = name
                if name == "vertex":
                    self.vertex_count = int(count)
                elif int(count) != 0:
                    fail(f"unsupported non-empty element: {line!r}")
            elif keyword == "property":
                if current_element != "vertex":
                    continue
                ptype, _, pname = rest.partition(" ")
                if ptype not in ("float", "float32"):
                    fail(f"unsupported property type: {line!r} "
                         "(only float32 vertex properties are supported)")
                self.properties.append(pname)
            elif keyword in ("ply", "format", "obj_info"):
                continue
            else:
                fail(f"unsupported header line: {line!r}")

        if self.vertex_count < 0:
            fail("no vertex element in header")
        if not self.properties:
            fail("vertex element declares no properties")
        for required in ("x", "y", "z", "opacity"):
            if required not in self.properties:
                fail(f"required property {required!r} not found")

    def column(self, name: str) -> int:
        return self.properties.index(name)

    def sh_degree(self) -> int:
        rest = sum(1 for p in self.properties if p.startswith("f_rest_"))
        if rest % 3 != 0:
            fail(f"f_rest_* count {rest} is not divisible by 3")
        degree = math.isqrt(rest // 3 + 1) - 1
        if 3 * ((degree + 1) ** 2 - 1) != rest:
            fail(f"f_rest_* count {rest} does not match any SH degree")
        return degree


def parse_aabb(text: str) -> tuple[float, ...]:
    parts = [float(v) for v in text.split(",")]
    if len(parts) != 6:
        fail("--aabb expects minx,miny,minz,maxx,maxy,maxz")
    if parts[0] > parts[3] or parts[1] > parts[4] or parts[2] > parts[5]:
        fail("--aabb min must not exceed max on any axis")
    return tuple(parts)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path, help="binary little-endian Gaussian PLY")
    parser.add_argument("--top-n", type=int, required=True,
                        help="keep the N most opaque Gaussians")
    parser.add_argument("--out", type=Path, required=True, help="output PLY path")
    parser.add_argument("--aabb", type=parse_aabb, default=None,
                        help="optional position filter applied before --top-n: "
                             "minx,miny,minz,maxx,maxy,maxz")
    args = parser.parse_args()
    if args.top_n <= 0:
        fail("--top-n must be positive")

    blob = args.source.read_bytes()
    header_end = blob.find(b"end_header\n")
    if header_end < 0:
        fail("no end_header found")
    body_offset = header_end + len(b"end_header\n")
    header = PlyHeader(blob[:body_offset])

    stride = len(header.properties)
    row_bytes = stride * 4
    n = header.vertex_count
    expected = body_offset + n * row_bytes
    if len(blob) < expected:
        fail(f"file truncated: expected {expected} bytes, found {len(blob)}")

    floats = array.array("f")
    floats.frombytes(blob[body_offset:expected])
    if sys.byteorder == "big":
        floats.byteswap()

    candidates = range(n)
    if args.aabb is not None:
        xs = floats[header.column("x")::stride]
        ys = floats[header.column("y")::stride]
        zs = floats[header.column("z")::stride]
        x0, y0, z0, x1, y1, z1 = args.aabb
        candidates = [i for i in candidates
                      if x0 <= xs[i] <= x1 and y0 <= ys[i] <= y1 and z0 <= zs[i] <= z1]
        if not candidates:
            fail("no Gaussians inside --aabb")

    opacities = floats[header.column("opacity")::stride]
    ranked = sorted(candidates, key=lambda i: (-opacities[i], i))
    selected = sorted(ranked[: args.top_n])

    out_lines = ["ply", "format binary_little_endian 1.0"]
    out_lines += [f"comment {c}" for c in header.comments]
    subset_note = f"subset: top {len(selected)} by opacity of {n} source gaussians"
    if args.aabb is not None:
        subset_note += ", aabb " + ",".join(repr(v) for v in args.aabb)
    out_lines.append(f"comment {subset_note}")
    out_lines.append(f"comment source sha256 {sha256_bytes(blob)}")
    out_lines.append(f"element vertex {len(selected)}")
    out_lines += [f"property float {p}" for p in header.properties]
    out_lines.append("end_header")
    out_header = ("\n".join(out_lines) + "\n").encode("ascii")

    body = bytearray()
    for i in selected:
        start = body_offset + i * row_bytes
        body += blob[start:start + row_bytes]
    out_blob = out_header + bytes(body)
    args.out.write_bytes(out_blob)

    provenance = {
        "schema": PROVENANCE_SCHEMA,
        "source": {
            "file": args.source.name,
            "sha256": sha256_bytes(blob),
            "bytes": len(blob),
            "gaussian_count": n,
            "sh_degree": header.sh_degree(),
            "property_order": header.properties,
        },
        "derivation": {
            "tool": "scripts/ply_subset.py",
            "selection": "aabb filter, then top-N opacity, original order preserved",
            "top_n": args.top_n,
            "aabb": list(args.aabb) if args.aabb is not None else None,
            "date_utc": datetime.datetime.now(datetime.timezone.utc)
                        .strftime("%Y-%m-%d"),
        },
        "output": {
            "file": args.out.name,
            "sha256": sha256_bytes(out_blob),
            "bytes": len(out_blob),
            "gaussian_count": len(selected),
            "sh_degree": header.sh_degree(),
        },
    }
    provenance_path = args.out.with_suffix(args.out.suffix + ".provenance.json")
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n",
                               encoding="ascii", newline="\n")

    print(f"{args.out}: {len(selected)} of {n} gaussians, "
          f"{len(out_blob)} bytes, SH degree {header.sh_degree()}")
    print(f"{provenance_path}: written")


if __name__ == "__main__":
    main()
