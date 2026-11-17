// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openstrata::gs::spz {

// The SPZ header: the decompressed 16-byte header of v1-v3, or the plaintext
// 32-byte header of v4. All fields are little-endian; the magic is consumed
// during validation and not stored. Layout facts are recorded in
// docs/reference/SPZ_FORMAT.md §4.
struct SpzHeader {
    std::uint32_t version = 0;
    std::uint32_t pointCount = 0;
    std::uint8_t shDegree = 0;
    std::uint8_t fractionalBits = 0;
    std::uint8_t flags = 0;
    // v1-v3 only. The v4 header has twelve reserved bytes instead; both are
    // accepted whatever they hold, as the reference reader does.
    std::uint8_t reserved = 0;
    // v4 only: the number of table-of-contents entries and the file offset of
    // the table, which follows the extension-record zone.
    std::uint8_t streamCount = 0;
    std::uint32_t tocByteOffset = 0;

    bool IsAntialiased() const noexcept
    {
        return (flags & 0x01) != 0;
    }
    bool HasExtensions() const noexcept
    {
        return (flags & 0x02) != 0;
    }

    // Per-point byte widths are fixed by the container version: v1 stores
    // float16 positions, v2+ 24-bit fixed point; v1-v2 store three 8-bit
    // quaternion components, v3+ packs smallest-three into four bytes. v4
    // changes only the container, so its attribute encodings are v3's.
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

// One extension record, `[u32 type][u32 byteLength][payload]`. `offset` and
// `size` locate the payload inside SpzPackedDocument::extensions.
struct SpzExtensionRecord {
    std::uint32_t type = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
};

// Extension record types the reader interprets (SPZ_FORMAT.md §5). Every
// other type is kept opaque and reported as ignored.
inline constexpr std::uint32_t kCoordinateSystemExtension = 0xADBE0003u;

// The attribute-major body with the container's quantized bytes left
// untouched. Dequantization into GaussianCloudData is the semantic decoder's
// job; keeping the payload in one buffer with spans avoids a second copy of
// every attribute. v1-v3 decompress one gzip member into it; v4 decompresses
// each zstd stream straight into its span.
struct SpzPackedDocument {
    struct Span {
        std::size_t offset = 0;
        std::size_t size = 0;
    };

    SpzHeader header;
    // The six attribute streams back to back: everything after the 16-byte
    // header in v1-v3, the concatenated zstd streams in v4.
    std::vector<unsigned char> payload;

    // Stream order as stored: positions, alphas, colors, scales, rotations,
    // spherical harmonics (SPZ_FORMAT.md §4).
    Span positions;
    Span alphas;
    Span colors;
    Span scales;
    Span rotations;
    Span sh;

    // Raw extension-record bytes when the header extensions flag (0x2) is
    // set: in v1-v3 they follow the attribute streams inside the gzip member,
    // in v4 they fill the plaintext zone between the header and the TOC.
    std::vector<unsigned char> extensions;
    // The records parsed from `extensions`, in file order. Parsing stops, as
    // in the reference reader, at the first record whose declared length runs
    // past the end; the bytes from there on are counted in
    // `unparsedExtensionBytes` and are not an error.
    std::vector<SpzExtensionRecord> extensionRecords;
    std::size_t unparsedExtensionBytes = 0;
    // v4 only: zstd skippable frames after the data frame of the last stream,
    // where newer writers put framed extension data. Never decompressed.
    std::uint64_t extensionFrameBytes = 0;

    const unsigned char* Bytes(const Span& span) const noexcept
    {
        return payload.data() + span.offset;
    }
};

// Owns every SPZ container concern: signature detection, gzip member framing
// and raw-DEFLATE decompression for v1-v3 (vendored miniz), the v4 table of
// contents and per-stream zstd decompression (vendored zstd, ADR 0002),
// header validation, extension-record parsing and the declared-frame check,
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

    // Metadata-only path. v1-v3: decompresses just the 16-byte header and
    // validates it, plus a compressed-size plausibility bound on the declared
    // point count. v4: validates the plaintext header, the extension-record
    // zone (including the declared coordinate frame), and the table of
    // contents, without decompressing anything. Neither touches the attribute
    // streams, so a valid header with a truncated or corrupt body succeeds
    // here and fails in Read(). A v1-v3 coordinate-frame record sits after
    // the streams, so only Read() can see it.
    bool ReadHeader(const std::string& path, SpzHeader* header,
                    std::string* error = nullptr) const;

    bool Read(const std::string& path, SpzPackedDocument* document,
              std::string* error = nullptr) const;
};

} // namespace openstrata::gs::spz
