# SPDX-License-Identifier: Apache-2.0
"""Generate deterministic SPZ container fixtures used by the reader tests.

The gzip member is assembled by hand (RFC 1952) rather than through the gzip
module so damaged framing — flipped trailer bytes, truncated members, data
after the member — can be constructed exactly. Payload bytes follow the
pattern (7*i + 3) % 256 so tests can verify attribute spans byte-for-byte.
"""

from __future__ import annotations

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


def main() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    valid_fixtures()
    invalid_fixtures()


if __name__ == "__main__":
    main()
