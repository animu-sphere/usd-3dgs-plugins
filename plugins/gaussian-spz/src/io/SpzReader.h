// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openstrata::gs::spz {

// The decompressed 16-byte SPZ v1-v3 header. All fields are little-endian in
// the stream; the magic is consumed during validation and not stored. Layout
// facts are recorded in docs/reference/SPZ_FORMAT.md §4.
struct SpzHeader {
    std::uint32_t version = 0;
    std::uint32_t pointCount = 0;
    std::uint8_t shDegree = 0;
    std::uint8_t fractionalBits = 0;
    std::uint8_t flags = 0;
    std::uint8_t reserved = 0;

    bool IsAntialiased() const noexcept { return (flags & 0x01) != 0; }
    bool HasExtensions() const noexcept { return (flags & 0x02) != 0; }

    // Per-point byte widths are fixed by the container version: v1 stores
    // float16 positions, v2+ 24-bit fixed point; v1-v2 store three 8-bit
    // quaternion components, v3 packs smallest-three into four bytes.
    std::size_t BytesPerPosition() const noexcept
    {
        return version == 1 ? 6 : 9;
    }
    std::size_t BytesPerRotation() const noexcept
    {
        return version >= 3 ? 4 : 3;
    }
    // Rest-only spherical-harmonics dimensions per color channel; the DC term
    // travels in the color stream, not here.
    std::size_t ShDimensions() const noexcept
    {
        const std::size_t side = static_cast<std::size_t>(shDegree) + 1;
        return side * side - 1;
    }
};

// The decompressed attribute-major body with the container's quantized bytes
// left untouched. Dequantization into GaussianCloudData is the semantic
// decoder's job; keeping the payload in one buffer with spans avoids a second
// copy of every attribute.
struct SpzPackedDocument {
    struct Span {
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    SpzHeader header;
    std::vector<unsigned char> payload; // everything after the 16-byte header

    // Stream order as stored: positions, alphas, colors, scales, rotations,
    // spherical harmonics (SPZ_FORMAT.md §4).
    Span positions;
    Span alphas;
    Span colors;
    Span scales;
    Span rotations;
    Span sh;

    // Raw extension records following the attribute streams when the header
    // extensions flag (0x2) is set. Opaque at container level.
    std::vector<unsigned char> extensions;

    const unsigned char* Bytes(const Span& span) const noexcept
    {
        return payload.data() + span.offset;
    }
};

// Owns every SPZ v1-v3 container concern: signature detection, gzip member
// framing, raw-DEFLATE decompression (vendored miniz), header validation,
// overflow-safe size math, and truncation/corruption/trailing-data detection.
// Errors carry stable GSPZ-**** container diagnostics. The reader performs no
// semantic dequantization and constructs no USD objects.
class SpzReader {
public:
    // Signature-only routing decision (design policy §7.6): true for a
    // plaintext NGSP container at offset 0 (the v4 layout) or a gzip member
    // whose first 16 decompressed bytes carry the SPZ magic. The version is
    // deliberately not checked here so an unsupported-version file reaches
    // Read() and fails with GSPZ-E003 instead of being silently disowned.
    bool CanRead(const std::string& path) const noexcept;

    // Metadata-only path: decompresses just the 16-byte header and validates
    // it, plus a compressed-size plausibility bound on the declared point
    // count. It never touches the attribute streams, so a valid header with a
    // truncated or corrupt body succeeds here and fails in Read().
    bool ReadHeader(
        const std::string& path,
        SpzHeader* header,
        std::string* error = nullptr) const;

    bool Read(
        const std::string& path,
        SpzPackedDocument* document,
        std::string* error = nullptr) const;
};

} // namespace openstrata::gs::spz
