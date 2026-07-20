# SPDX-License-Identifier: Apache-2.0
"""Generate deterministic SPZ fixtures used by the reader and decoder tests.

The gzip member is assembled by hand (RFC 1952) rather than through the gzip
module so damaged framing — flipped trailer bytes, truncated members, data
after the member — can be constructed exactly. Container-reader fixtures fill
their payload with the pattern (7*i + 3) % 256 so tests can verify attribute
spans byte-for-byte.

The semantic-decoder fixtures instead encode known floating-point Gaussians
through the reference serializer's exact quantization formulas
(https://github.com/nianticlabs/spz, splat-utils.h / load-spz.cc). The C++
decoder test knows the same source values, applies the documented RUB->RDF
reference-frame conversion (SPZ_MAPPING.md §4-§5), and checks the decoded
cloud within per-attribute quantization tolerances. Encoding a known value and
decoding it back is a genuine inverse check: a sign or ordering error does not
round-trip.
"""

from __future__ import annotations

import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).parents[1] / "tests" / "fixtures"

MAGIC = 0x5053474E  # "NGSP" little-endian


def deflate(data: bytes) -> bytes:
    compressor = zlib.compressobj(level=9, wbits=-15)
    return compressor.compress(data) + compressor.flush()


def gzip_member(
    data: bytes,
    body: bytes | None = None,
    crc: int | None = None,
    isize: int | None = None,
    extra: bytes | None = None,
    name: bytes | None = None,
    comment: bytes | None = None,
    fhcrc: bool | int = False,
) -> bytes:
    """A single gzip member: header, raw-DEFLATE body, CRC32+ISIZE trailer.
    `body`, `crc`, and `isize` overrides build damaged members; `extra`,
    `name`, `comment`, and `fhcrc` populate the optional RFC 1952 header
    fields (`fhcrc=True` stores the correct CRC16, an int stores that
    value verbatim)."""
    flg = 0x00
    if extra is not None:
        flg |= 0x04
    if name is not None:
        flg |= 0x08
    if comment is not None:
        flg |= 0x10
    if fhcrc is not False:
        flg |= 0x02
    header = bytes([0x1F, 0x8B, 0x08, flg, 0, 0, 0, 0, 0x00, 0xFF])
    if extra is not None:
        header += struct.pack("<H", len(extra)) + extra
    if name is not None:
        header += name + b"\x00"
    if comment is not None:
        header += comment + b"\x00"
    if fhcrc is not False:
        value = zlib.crc32(header) & 0xFFFF if fhcrc is True else fhcrc
        header += struct.pack("<H", value)
    if body is None:
        body = deflate(data)
    if crc is None:
        crc = zlib.crc32(data) & 0xFFFFFFFF
    if isize is None:
        isize = len(data) & 0xFFFFFFFF
    return header + body + struct.pack("<II", crc, isize)


def spz_header(
    version: int,
    count: int,
    sh_degree: int,
    fractional_bits: int = 12,
    flags: int = 0,
    reserved: int = 0,
) -> bytes:
    return struct.pack(
        "<IIIBBBB", MAGIC, version, count, sh_degree, fractional_bits,
        flags, reserved)


def payload_size(version: int, count: int, sh_degree: int) -> int:
    per_position = 6 if version == 1 else 9
    per_rotation = 4 if version >= 3 else 3
    sh_dims = (sh_degree + 1) ** 2 - 1
    return count * (per_position + 1 + 3 + 3 + per_rotation + 3 * sh_dims)


def payload(size: int) -> bytes:
    return bytes((7 * i + 3) % 256 for i in range(size))


def spz_stream(version: int, count: int, sh_degree: int, **kwargs) -> bytes:
    return spz_header(version, count, sh_degree, **kwargs) + payload(
        payload_size(version, count, sh_degree))


def write(name: str, data: bytes) -> None:
    (ROOT / name).write_bytes(data)


def valid_fixtures() -> None:
    write("minimal-v1.spz", gzip_member(spz_stream(1, 1, 0)))
    write("minimal-v2.spz", gzip_member(spz_stream(2, 1, 0)))
    write("minimal-v3.spz", gzip_member(spz_stream(3, 1, 0)))
    write("three-points-degree1-v2.spz", gzip_member(spz_stream(2, 3, 1)))
    write("degree3-v2.spz", gzip_member(spz_stream(2, 1, 3)))
    # Spec-maximum SH degree: structurally valid at container level; the
    # accept-or-reject decision belongs to the semantic decoder
    # (SPZ_FORMAT.md §7).
    write("degree4-v2.spz", gzip_member(spz_stream(2, 1, 4)))
    # Antialiased (0x1) + extensions (0x2); eight opaque extension bytes
    # follow the attribute streams inside the decompressed stream.
    write("extensions-v2.spz", gzip_member(
        spz_stream(2, 1, 0, flags=0x03) + b"EXTBYTES"))
    # Every optional RFC 1952 header field at once, with a correct CRC16.
    write("header-fields-v2.spz", gzip_member(
        spz_stream(2, 1, 0), extra=b"XTRA", name=b"fixture.spz",
        comment=b"generated fixture", fhcrc=True))
    # An FNAME longer than the 64 KiB CanRead prefix: the first bounded
    # attempt is inconclusive and the single 1 MiB retry must resolve it.
    write("long-fname-v2.spz", gzip_member(
        spz_stream(2, 1, 0), name=b"n" * 70_000))


def invalid_fixtures() -> None:
    # Wrong signature entirely, and a gzip member of something else.
    write("not-spz.spz", b"this file is not an SPZ container at all")
    write("gzip-not-spz.spz", gzip_member(
        b"a gzip member holding something other than SPZ"))

    # Plaintext NGSP layouts: v4 is real but unsupported (E003); v2 was
    # never plaintext (E004). The v4 fixture follows the 32-byte header:
    # magic, version, numPoints, shDegree, fractionalBits, flags,
    # numStreams, tocByteOffset, reserved[12].
    v4_header = struct.pack(
        "<IIIBBBBI", MAGIC, 4, 100, 1, 12, 0, 6, 32) + bytes(12)
    write("plaintext-v4.spz", v4_header + payload(64))
    write("plaintext-v2.spz", spz_stream(2, 1, 0))

    # Header-field rejections inside a well-formed gzip member.
    write("version-0.spz", gzip_member(spz_stream(0, 1, 0)))
    write("version-5.spz", gzip_member(spz_stream(5, 1, 0)))
    write("empty-points-v2.spz", gzip_member(spz_header(2, 0, 0)))
    write("huge-count-v2.spz", gzip_member(spz_header(2, 0x80000000, 0)))
    write("sh-degree-5-v2.spz", gzip_member(
        spz_header(2, 1, 5) + payload(19)))

    # A count that is in range but provably not present in the compressed
    # bytes (DEFLATE cannot expand 1032:1 over this member).
    write("count-exceeds-stream-v2.spz", gzip_member(
        spz_header(2, 1_000_000, 0) + payload(19)))

    # Damaged framing.
    write("truncated-gzip-header.spz", bytes([0x1F, 0x8B, 0x08, 0x00]))
    write("short-stream.spz", gzip_member(b"0123456789"))

    # Attribute streams cut short. truncated-payload ends the DEFLATE stream
    # cleanly with a consistent trailer, so only the full read fails;
    # truncated-deflate cuts the compressed bytes mid-stream.
    short = spz_header(2, 3, 0) + payload(30)  # header declares 57
    write("truncated-payload-v2.spz", gzip_member(short))
    full = gzip_member(spz_stream(2, 3, 0))
    write("truncated-deflate-v2.spz", full[: 10 + (len(full) - 18) * 3 // 5])

    # Undecodable DEFLATE: BFINAL=1 with the reserved block type 11.
    write("corrupt-deflate.spz", bytes([0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0,
                                        0x00, 0xFF, 0x07]) + bytes(15))

    # Trailer mismatches on an otherwise valid member.
    minimal = spz_stream(2, 1, 0)
    write("bad-crc-v2.spz", gzip_member(
        minimal, crc=(zlib.crc32(minimal) ^ 0xFF) & 0xFFFFFFFF))
    write("bad-isize-v2.spz", gzip_member(minimal, isize=len(minimal) ^ 0xFF))

    # A wrong FHCRC over an otherwise valid member: damaged header (E004).
    write("bad-fhcrc-v2.spz", gzip_member(minimal, fhcrc=0xBEEF))

    # Trailing data both inside the decompressed stream (without the
    # extensions flag) and after the gzip member.
    write("trailing-decompressed-v2.spz", gzip_member(minimal + b"EXTRA!"))
    write("trailing-after-member-v2.spz", gzip_member(minimal) + b"JUNKJUNK")


# --------------------------------------------------------------------------
# Semantic-decoder fixtures: known Gaussians encoded with the reference
# quantization formulas.
# --------------------------------------------------------------------------

SQRT1_2 = 0.7071067811865476
COLOR_SCALE = 0.15


def to_uint8(value: float) -> int:
    return max(0, min(255, int(round(value))))


def enc_position_v2(xyz: tuple[float, float, float], fractional_bits: int) -> bytes:
    """24-bit little-endian two's-complement fixed point."""
    out = b""
    for component in xyz:
        fixed = int(round(component * (1 << fractional_bits))) & 0xFFFFFF
        out += bytes([fixed & 0xFF, (fixed >> 8) & 0xFF, (fixed >> 16) & 0xFF])
    return out


def enc_position_v1(xyz: tuple[float, float, float]) -> bytes:
    """Three IEEE 754 binary16 components, little-endian."""
    return b"".join(struct.pack("<e", component) for component in xyz)


def enc_scale(log_scales: tuple[float, float, float]) -> bytes:
    return bytes(to_uint8((s + 10.0) * 16.0) for s in log_scales)


def enc_alpha(opacity: float) -> bytes:
    # The decoder reports opacity as byte / 255 directly, so encoding the
    # target opacity is a single scale.
    return bytes([to_uint8(opacity * 255.0)])


def enc_color(dc: tuple[float, float, float]) -> bytes:
    return bytes(to_uint8(c * (COLOR_SCALE * 255.0) + 0.5 * 255.0) for c in dc)


def _normalize_wxyz(quat_wxyz: tuple[float, float, float, float]):
    w, x, y, z = quat_wxyz
    norm = math.sqrt(w * w + x * x + y * y + z * z)
    return w / norm, x / norm, y / norm, z / norm


def enc_rotation_first_three(quat_wxyz) -> bytes:
    """v1/v2: store x, y, z after forcing w >= 0 (reference packQuaternionFirstThree)."""
    w, x, y, z = _normalize_wxyz(quat_wxyz)
    sign = -127.5 if w < 0 else 127.5
    return bytes([
        to_uint8(x * sign + 127.5),
        to_uint8(y * sign + 127.5),
        to_uint8(z * sign + 127.5)])


def enc_rotation_smallest_three(quat_wxyz) -> bytes:
    """v3: 2-bit largest index + three 10-bit signed magnitudes scaled by 1/sqrt2
    (reference packQuaternionSmallestThree). Component order is (x, y, z, w)."""
    w, x, y, z = _normalize_wxyz(quat_wxyz)
    q = [x, y, z, w]
    i_largest = 0
    for i in range(1, 4):
        if abs(q[i]) > abs(q[i_largest]):
            i_largest = i
    negate = q[i_largest] < 0
    comp = i_largest
    for i in range(4):
        if i == i_largest:
            continue
        negbit = (1 if q[i] < 0 else 0) ^ (1 if negate else 0)
        magnitude = int(((1 << 9) - 1) * (abs(q[i]) / SQRT1_2) + 0.5)
        comp = (comp << 10) | (negbit << 9) | magnitude
    return struct.pack("<I", comp)


def enc_sh(value: float) -> int:
    # quantizeSH with bucket size 1 (no entropy bucketing): round(x*128)+128.
    return max(0, min(255, int(round(value * 128.0)) + 128))


def sh_triple(point: int, k: int) -> tuple[float, float, float]:
    """Source RGB for rest coefficient `k` of `point`, mirrored by the C++ test.

    Distinct across point, coefficient, and channel, so a wrong flip sign, a
    coefficient swap, a channel swap, and a point-stride error each change the
    decoded values. All are multiples of 1/128 (the SH quantization step).
    """
    a = (k + 1) / 32.0
    b = (k + 2) / 32.0
    c = (k + 3) / 64.0
    return (a, -b, c) if point == 0 else (-a, c, -b)


def enc_sh_stream(points_sh) -> bytes:
    """points_sh[point][coefficient] = (r, g, b); channel is the inner axis."""
    out = bytearray()
    for coefficients in points_sh:
        for rgb in coefficients:
            out += bytes(enc_sh(channel) for channel in rgb)
    return bytes(out)


def decoder_fixtures() -> None:
    # A two-point degree-1 v2 asset exercising every attribute plus the SH
    # ordering and the per-coefficient RUB->RDF sign flips. Positions and log
    # scales are chosen to land on exact quantization points; the C++ test
    # mirrors these source values (test_gaussian_spz_decoder.cpp).
    frac = 12
    positions = [(1.0, 2.0, -0.5), (-3.0, 0.25, 4.0)]
    log_scales = [(0.0, 1.0, -1.0), (0.5, -0.5, 0.0)]
    quaternions = [
        (1.0, 0.0, 0.0, 0.0),                       # identity
        (0.70710678, 0.70710678, 0.0, 0.0)]         # 90 deg about +X
    opacities = [0.8, 0.6]
    dc = [(0.0, 0.5, -0.5), (0.9, -0.9, 0.0)]
    sh = [
        [(0.1, 0.2, 0.3), (-0.1, -0.2, -0.3), (0.4, -0.4, 0.5)],
        [(0.05, -0.05, 0.15), (0.25, 0.35, -0.45), (-0.6, 0.6, 0.1)]]

    stream = spz_header(2, len(positions), 1, fractional_bits=frac)
    stream += b"".join(enc_position_v2(p, frac) for p in positions)
    stream += b"".join(enc_alpha(o) for o in opacities)
    stream += b"".join(enc_color(c) for c in dc)
    stream += b"".join(enc_scale(s) for s in log_scales)
    stream += b"".join(enc_rotation_first_three(q) for q in quaternions)
    stream += enc_sh_stream(sh)
    write("decode-degree1-v2.spz", gzip_member(stream))

    # A two-point degree-3 asset covering every rest coefficient, so all 15
    # RUB->RDF sign flips (bands 1-3) and the degree-3 Gaussian stride are
    # pinned rather than just the three band-1 flips a degree-1 fixture
    # reaches. Every SH value is an exact multiple of 1/128, so quantization
    # round-trips without error, and the three channels of a coefficient are
    # always distinct so a channel swap is visible too.
    degree3_positions = [(0.5, 1.0, -1.5), (-2.0, 0.75, 3.0)]
    stream = spz_header(2, len(degree3_positions), 3, fractional_bits=frac)
    stream += b"".join(enc_position_v2(p, frac) for p in degree3_positions)
    stream += b"".join(enc_alpha(o) for o in (0.25, 0.75))
    stream += b"".join(
        enc_color(c) for c in [(0.2, -0.2, 0.4), (-0.4, 0.1, -0.1)])
    stream += b"".join(
        enc_scale(s) for s in [(0.0, 0.0, 0.0), (1.0, -1.0, 0.5)])
    stream += b"".join(enc_rotation_first_three(q) for q in [
        (1.0, 0.0, 0.0, 0.0), (0.5, 0.5, 0.5, 0.5)])
    stream += enc_sh_stream(
        [[sh_triple(point, k) for k in range(15)] for point in range(2)])
    write("decode-degree3-v2.spz", gzip_member(stream))

    # Single-point degree-0 fixtures isolating the version-specific paths.
    def single(version: int, position, quat, *, frac_bits=12) -> bytes:
        stream = spz_header(version, 1, 0, fractional_bits=frac_bits)
        if version == 1:
            stream += enc_position_v1(position)
        else:
            stream += enc_position_v2(position, frac_bits)
        stream += enc_alpha(0.5)
        stream += enc_color((0.0, 0.0, 0.0))
        stream += enc_scale((0.0, 0.0, 0.0))
        if version >= 3:
            stream += enc_rotation_smallest_three(quat)
        else:
            stream += enc_rotation_first_three(quat)
        return stream

    # v1 float16 positions; a first-three rotation about +X.
    write("decode-v1.spz", gzip_member(
        single(1, (1.5, -2.5, 0.75), (0.92387953, 0.38268343, 0.0, 0.0))))
    # v3 smallest-three rotation whose largest component is x, not w.
    write("decode-v3.spz", gzip_member(
        single(3, (2.0, -1.0, 0.5), (0.2, 0.8, 0.4, 0.4))))

    # Degree 4 is valid SPZ but beyond the shared model: unsupported, not
    # malformed (GSPZ-E011).
    write("decode-degree4-v2.spz", gzip_member(spz_stream(2, 1, 4)))

    # A non-finite float16 position: the one place a bad value enters the
    # pipeline (GSPZ-E012). Header then inf-x, finite y/z, then the remaining
    # degree-0 attributes so the container itself stays well-formed.
    bad = spz_header(1, 1, 0)
    bad += struct.pack("<e", float("inf")) + struct.pack("<e", 0.0) + struct.pack("<e", 0.0)
    bad += enc_alpha(0.5) + enc_color((0.0, 0.0, 0.0)) + enc_scale((0.0, 0.0, 0.0))
    bad += enc_rotation_first_three((1.0, 0.0, 0.0, 0.0))
    write("decode-nonfinite-v1.spz", gzip_member(bad))


def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    valid_fixtures()
    invalid_fixtures()
    decoder_fixtures()


if __name__ == "__main__":
    main()
