// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSpzDiagnostics.h"
#include "io/SpzReader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace gsspz = openstrata::gs::spz;

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n";       \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

std::string Fixture(const char* name)
{
    return (std::filesystem::path(GAUSSIAN_SPZ_FIXTURE_DIR) / name).string();
}

bool HasCode(const std::string& error, const char* code)
{
    return error.rfind(std::string("[") + code + "]", 0) == 0;
}

// Payload bytes are generated as (7*i + 3) % 256 by
// tools/generate_fixtures.py.
bool PayloadMatchesPattern(const gsspz::SpzPackedDocument& document)
{
    for (std::size_t i = 0; i < document.payload.size(); ++i) {
        if (document.payload[i] !=
            static_cast<unsigned char>((7 * i + 3) % 256)) {
            return false;
        }
    }
    return true;
}

bool SpanEquals(const gsspz::SpzPackedDocument::Span& span, std::size_t offset,
                std::size_t size)
{
    return span.offset == offset && span.size == size;
}

// A valid single-point degree-0 container in each supported version. The
// versions differ only in per-point widths: v1 stores float16 positions,
// v3 four-byte smallest-three rotations. v4 has v3's widths in the zstd
// container, so its payload is the same pattern.
void TestMinimalContainer(const char* fixture, std::uint32_t version,
                          std::size_t positionBytes, std::size_t rotationBytes)
{
    const gsspz::SpzReader reader;
    const std::string path = Fixture(fixture);
    CHECK(reader.CanRead(path));

    gsspz::SpzHeader header;
    std::string error;
    CHECK(reader.ReadHeader(path, &header, &error));
    CHECK(error.empty());
    CHECK(header.version == version);
    CHECK(header.pointCount == 1);
    CHECK(header.shDegree == 0);
    CHECK(header.fractionalBits == 12);
    CHECK(header.flags == 0);
    CHECK(header.reserved == 0);

    gsspz::SpzPackedDocument document;
    CHECK(reader.Read(path, &document, &error));
    CHECK(error.empty());
    CHECK(document.header.version == version);
    const std::size_t expected = positionBytes + 1 + 3 + 3 + rotationBytes;
    CHECK(document.payload.size() == expected);
    CHECK(PayloadMatchesPattern(document));
    CHECK(document.extensions.empty());
    CHECK(document.extensionRecords.empty());
    CHECK(document.extensionFrameBytes == 0);

    std::size_t cursor = 0;
    CHECK(SpanEquals(document.positions, cursor, positionBytes));
    cursor += positionBytes;
    CHECK(SpanEquals(document.alphas, cursor, 1));
    cursor += 1;
    CHECK(SpanEquals(document.colors, cursor, 3));
    cursor += 3;
    CHECK(SpanEquals(document.scales, cursor, 3));
    cursor += 3;
    CHECK(SpanEquals(document.rotations, cursor, rotationBytes));
    cursor += rotationBytes;
    CHECK(SpanEquals(document.sh, cursor, 0));
}

void TestMultiPointShLayout()
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(
        reader.Read(Fixture("three-points-degree1-v2.spz"), &document, &error));
    CHECK(document.header.pointCount == 3);
    CHECK(document.header.shDegree == 1);
    CHECK(document.payload.size() == 84);
    CHECK(PayloadMatchesPattern(document));
    CHECK(SpanEquals(document.positions, 0, 27));
    CHECK(SpanEquals(document.alphas, 27, 3));
    CHECK(SpanEquals(document.colors, 30, 9));
    CHECK(SpanEquals(document.scales, 39, 9));
    CHECK(SpanEquals(document.rotations, 48, 9));
    CHECK(SpanEquals(document.sh, 57, 27));
}

// Rest-only SH sizing at the higher degrees: (degree+1)^2 - 1 dimensions per
// channel. Degree 4 is the specification maximum and must pass the container
// stage; whether the semantic decoder accepts it is a separate, later
// decision (SPZ_FORMAT.md §7).
void TestHighDegreeShSizing(const char* fixture, int degree,
                            std::size_t shBytes)
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(reader.Read(Fixture(fixture), &document, &error));
    CHECK(document.header.shDegree == degree);
    CHECK(document.sh.size == shBytes);
    CHECK(document.payload.size() == 19 + shBytes);
}

// Optional RFC 1952 header fields (FEXTRA, FNAME, FCOMMENT, FHCRC) must be
// skipped — and the FHCRC verified — without disturbing the streams behind
// them. The long-FNAME fixture pushes the magic past the 64 KiB CanRead
// prefix, so it also pins the bounded one-retry path.
// v4 uses v3 widths: 20 bytes per point before the SH stream, not 19.
void TestV4DegreeFourSizing()
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(reader.Read(Fixture("degree4-v4.spz"), &document, &error));
    CHECK(document.header.shDegree == 4);
    CHECK(document.sh.size == 72);
    CHECK(document.payload.size() == 20 + 72);
}

void TestGzipHeaderFields()
{
    const gsspz::SpzReader reader;
    for (const char* fixture : {"header-fields-v2.spz", "long-fname-v2.spz"}) {
        const std::string path = Fixture(fixture);
        CHECK(reader.CanRead(path));

        gsspz::SpzHeader header;
        std::string error;
        CHECK(reader.ReadHeader(path, &header, &error));
        CHECK(header.pointCount == 1);

        gsspz::SpzPackedDocument document;
        CHECK(reader.Read(path, &document, &error));
        CHECK(document.payload.size() == 19);
        CHECK(PayloadMatchesPattern(document));
    }
}

void TestExtensionsPreserved()
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(reader.Read(Fixture("extensions-v2.spz"), &document, &error));
    CHECK(document.header.IsAntialiased());
    CHECK(document.header.HasExtensions());
    CHECK(document.payload.size() == 19);
    CHECK(document.extensions.size() == 8);
    CHECK(std::string(document.extensions.begin(), document.extensions.end()) ==
          "EXTBYTES");
    // "EXTB" would be a type and "YTES" a length far past the end, so the
    // bytes do not form a record: kept, unparsed, and not an error.
    CHECK(document.extensionRecords.empty());
    CHECK(document.unparsedExtensionBytes == 8);
}

// The v4 layout with six streams: each zstd stream decodes into its span, and
// the spans tile the payload exactly as the v1-v3 single stream does.
void TestV4MultiStreamLayout()
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(
        reader.Read(Fixture("three-points-degree1-v4.spz"), &document, &error));
    CHECK(error.empty());
    CHECK(document.header.version == 4);
    CHECK(document.header.streamCount == 6);
    CHECK(document.header.tocByteOffset == 32);
    CHECK(document.payload.size() == 87);
    CHECK(PayloadMatchesPattern(document));
    CHECK(SpanEquals(document.positions, 0, 27));
    CHECK(SpanEquals(document.alphas, 27, 3));
    CHECK(SpanEquals(document.colors, 30, 9));
    CHECK(SpanEquals(document.scales, 39, 9));
    CHECK(SpanEquals(document.rotations, 48, 12));
    CHECK(SpanEquals(document.sh, 60, 27));
}

// v4 extension data: records in the plaintext zone, parsed by type and
// length, and skippable frames after the last stream, bounded but never
// decompressed. A RUB coordinate-system record is accepted.
void TestV4Extensions()
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(reader.Read(Fixture("extensions-v4.spz"), &document, &error));
    CHECK(error.empty());
    CHECK(document.header.IsAntialiased());
    CHECK(document.header.HasExtensions());
    CHECK(document.header.tocByteOffset == 32 + 16 + 12);
    CHECK(document.extensionRecords.size() == 2);
    if (document.extensionRecords.size() == 2) {
        const gsspz::SpzExtensionRecord& unknown = document.extensionRecords[0];
        CHECK(unknown.type == 0x12345678u);
        CHECK(unknown.size == 8);
        const auto first = document.extensions.begin() +
                           static_cast<std::ptrdiff_t>(unknown.offset);
        CHECK(std::string(first, first + 8) == "EXTBYTES");
        CHECK(document.extensionRecords[1].type ==
              gsspz::kCoordinateSystemExtension);
    }
    CHECK(document.unparsedExtensionBytes == 0);
    // Skippable frame: 8-byte frame header plus "FRAMEDATA".
    CHECK(document.extensionFrameBytes == 17);
    CHECK(PayloadMatchesPattern(document));

    for (const char* fixture :
         {"coordinate-unspecified-v4.spz", "coordinate-rub-v2.spz"}) {
        CHECK(reader.Read(Fixture(fixture), &document, &error));
        CHECK(document.extensionRecords.size() == 1);
    }
}

// Frames without a recorded content size, with checksums, and a header whose
// reserved bytes are not zero: all accepted, as the reference reader does.
void TestV4Framing()
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(reader.Read(Fixture("framing-v4.spz"), &document, &error));
    CHECK(error.empty());
    CHECK(document.header.pointCount == 2);
    CHECK(document.payload.size() == 2 * (9 + 1 + 3 + 3 + 4 + 9));
    CHECK(PayloadMatchesPattern(document));
}

void TestReadFailure(const char* fixture, const char* code)
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(!reader.Read(Fixture(fixture), &document, &error));
    if (!HasCode(error, code)) {
        std::cerr << fixture << ": expected " << code << ", got: " << error
                  << "\n";
        ++failures;
    }
}

// Metadata-only reads validate framing and header but never touch the
// attribute streams, so a valid header over a damaged body succeeds here
// and fails in Read() — the documented division (design policy §12.3).
void TestHeaderOnlySemantics()
{
    const gsspz::SpzReader reader;
    gsspz::SpzHeader header;
    std::string error;

    CHECK(reader.ReadHeader(Fixture("truncated-payload-v2.spz"), &header,
                            &error));
    CHECK(header.pointCount == 3);
    CHECK(reader.ReadHeader(Fixture("bad-crc-v2.spz"), &header, &error));

    // Header-level defects fail with the same codes as the full read.
    CHECK(!reader.ReadHeader(Fixture("version-5.spz"), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kUnsupportedVersion));
    CHECK(!reader.ReadHeader(Fixture("empty-points-v2.spz"), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kEmptyPointSet));
    CHECK(!reader.ReadHeader(Fixture("count-exceeds-stream-v2.spz"), &header,
                             &error));
    CHECK(HasCode(error, gsspz::diag::kTruncatedContainer));
    error.clear();
    CHECK(!reader.ReadHeader(Fixture("shared-limit-v2.spz"), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kImportLimitExceeded));

    // v4 keeps its whole structure in plaintext, so the metadata read checks
    // the TOC and the declared frame too; only the frames are left to Read().
    error.clear();
    CHECK(reader.ReadHeader(Fixture("extensions-v4.spz"), &header, &error));
    CHECK(header.version == 4);
    CHECK(header.shDegree == 1);
    CHECK(header.streamCount == 6);
    for (const char* fixture :
         {"corrupt-frame-v4.spz", "bad-checksum-v4.spz",
          "truncated-frame-v4.spz", "second-frame-v4.spz"}) {
        CHECK(reader.ReadHeader(Fixture(fixture), &header, &error));
    }
    const struct {
        const char* fixture;
        const char* code;
    } headerFailures[] = {
        {"coordinate-rdf-v4.spz", gsspz::diag::kUnsupportedCoordinateSystem},
        {"stream-size-v4.spz", gsspz::diag::kMalformedContainer},
        {"toc-short-v4.spz", gsspz::diag::kMalformedContainer},
        {"toc-overlong-v4.spz", gsspz::diag::kTruncatedContainer},
        {"shared-limit-v4.spz", gsspz::diag::kImportLimitExceeded},
        {"plaintext-v5.spz", gsspz::diag::kUnsupportedVersion},
    };
    for (const auto& failure : headerFailures) {
        error.clear();
        CHECK(!reader.ReadHeader(Fixture(failure.fixture), &header, &error));
        if (!HasCode(error, failure.code)) {
            std::cerr << failure.fixture << " (header): expected "
                      << failure.code << ", got: " << error << "\n";
            ++failures;
        }
    }
    // A v1-v3 coordinate-system record follows the streams, so only the full
    // read can refuse it.
    CHECK(reader.ReadHeader(Fixture("coordinate-rdf-v2.spz"), &header, &error));
}

// CanRead is signature-only (§7.6): every structurally identifiable SPZ
// container is claimed — including unsupported versions and damaged bodies,
// which Read() must explain with a specific code — while non-SPZ bytes and
// unreadable gzip are declined.
void TestCanRead()
{
    const gsspz::SpzReader reader;

    CHECK(reader.CanRead(Fixture("plaintext-v4.spz")));
    CHECK(reader.CanRead(Fixture("plaintext-v5.spz")));
    CHECK(reader.CanRead(Fixture("minimal-v4.spz")));
    CHECK(reader.CanRead(Fixture("truncated-header-v4.spz")));
    CHECK(reader.CanRead(Fixture("corrupt-frame-v4.spz")));
    CHECK(reader.CanRead(Fixture("coordinate-rdf-v4.spz")));
    CHECK(reader.CanRead(Fixture("plaintext-v2.spz")));
    CHECK(reader.CanRead(Fixture("version-0.spz")));
    CHECK(reader.CanRead(Fixture("version-5.spz")));
    CHECK(reader.CanRead(Fixture("empty-points-v2.spz")));
    CHECK(reader.CanRead(Fixture("huge-count-v2.spz")));
    CHECK(reader.CanRead(Fixture("shared-limit-v2.spz")));
    CHECK(reader.CanRead(Fixture("sh-degree-5-v2.spz")));
    CHECK(reader.CanRead(Fixture("truncated-payload-v2.spz")));
    CHECK(reader.CanRead(Fixture("truncated-deflate-v2.spz")));
    CHECK(reader.CanRead(Fixture("bad-crc-v2.spz")));
    CHECK(reader.CanRead(Fixture("trailing-after-member-v2.spz")));

    CHECK(!reader.CanRead(Fixture("not-spz.spz")));
    CHECK(!reader.CanRead(Fixture("gzip-not-spz.spz")));
    CHECK(!reader.CanRead(Fixture("bad-fhcrc-v2.spz")));
    CHECK(!reader.CanRead(Fixture("short-stream.spz")));
    CHECK(!reader.CanRead(Fixture("corrupt-deflate.spz")));
    CHECK(!reader.CanRead(Fixture("truncated-gzip-header.spz")));
    CHECK(!reader.CanRead(Fixture("does-not-exist.spz")));
}

// The real-size source bound (IMPORT_LIMITS.md): a gzip-signed file one byte
// past 1 GiB. resize_file grows it without writing the hole (sparse on Linux
// and macOS, an unwritten extension on NTFS), and both reads must refuse it
// from its size before any bulk read.
void TestOversizedSource()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "gaussian-spz-reader-oversized-source";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "oversized.spz";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const unsigned char gzip[] = {0x1f, 0x8b, 0x08, 0x00};
        out.write(reinterpret_cast<const char*>(gzip), sizeof gzip);
        CHECK(static_cast<bool>(out));
    }
    std::error_code grown;
    std::filesystem::resize_file(path, 1024ull * 1024ull * 1024ull + 1, grown);
    CHECK(!grown);

    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(!reader.Read(path.string(), &document, &error));
    CHECK(HasCode(error, gsspz::diag::kImportLimitExceeded));
    CHECK(error.find("-byte input limit") != std::string::npos);

    gsspz::SpzHeader header;
    error.clear();
    CHECK(!reader.ReadHeader(path.string(), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kImportLimitExceeded));

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

// The 64 MiB extension budget in v4, where the zone is plaintext and a
// committed fixture would be 64 MiB. A valid v4 header declares a zone one
// byte past the budget; the file is grown sparsely to hold it and its TOC.
// The header-only read refuses it from the 64 KiB prefix; the full read
// refuses it before copying or parsing the zone.
void TestOversizedV4ExtensionZone()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "gaussian-spz-reader-oversized-v4-zone";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "oversized-zone-v4.spz";
    const std::uint32_t tocOffset = 32 + 64u * 1024u * 1024u + 1u;
    {
        unsigned char header[32] = {0x4e, 0x47, 0x53, 0x50, 4, 0,  0,    0,
                                    1,    0,    0,    0,    0, 12, 0x02, 5};
        for (int i = 0; i < 4; ++i) {
            header[16 + i] =
                static_cast<unsigned char>((tocOffset >> (8 * i)) & 0xffu);
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(header), sizeof header);
        CHECK(static_cast<bool>(out));
    }
    std::error_code grown;
    std::filesystem::resize_file(path, tocOffset + 5 * 16, grown);
    CHECK(!grown);

    const gsspz::SpzReader reader;
    gsspz::SpzHeader header;
    std::string error;
    CHECK(!reader.ReadHeader(path.string(), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kImportLimitExceeded));
    gsspz::SpzPackedDocument document;
    error.clear();
    CHECK(!reader.Read(path.string(), &document, &error));
    CHECK(HasCode(error, gsspz::diag::kImportLimitExceeded));

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

void TestInternalMisuse()
{
    const gsspz::SpzReader reader;
    std::string error;
    CHECK(!reader.Read(Fixture("minimal-v2.spz"), nullptr, &error));
    CHECK(HasCode(error, gsspz::diag::kInternalError));
    CHECK(!reader.ReadHeader(Fixture("minimal-v2.spz"), nullptr, &error));
    CHECK(HasCode(error, gsspz::diag::kInternalError));
}

} // namespace

int main()
{
    TestMinimalContainer("minimal-v1.spz", 1, 6, 3);
    TestMinimalContainer("minimal-v2.spz", 2, 9, 3);
    TestMinimalContainer("minimal-v3.spz", 3, 9, 4);
    TestMinimalContainer("minimal-v4.spz", 4, 9, 4);
    TestMultiPointShLayout();
    TestV4MultiStreamLayout();
    TestHighDegreeShSizing("degree3-v2.spz", 3, 45);
    TestHighDegreeShSizing("degree4-v2.spz", 4, 72);
    TestV4DegreeFourSizing();
    TestGzipHeaderFields();
    TestExtensionsPreserved();
    TestV4Extensions();
    TestV4Framing();

    TestReadFailure("not-spz.spz", gsspz::diag::kNotSpzContainer);
    TestReadFailure("gzip-not-spz.spz", gsspz::diag::kNotSpzContainer);
    TestReadFailure("plaintext-v4.spz", gsspz::diag::kTruncatedContainer);
    TestReadFailure("plaintext-v5.spz", gsspz::diag::kUnsupportedVersion);
    TestReadFailure("plaintext-v2.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("version-0.spz", gsspz::diag::kUnsupportedVersion);
    TestReadFailure("version-5.spz", gsspz::diag::kUnsupportedVersion);
    TestReadFailure("empty-points-v2.spz", gsspz::diag::kEmptyPointSet);
    TestReadFailure("huge-count-v2.spz", gsspz::diag::kInvalidPointCount);
    TestReadFailure("shared-limit-v2.spz", gsspz::diag::kImportLimitExceeded);
    TestReadFailure("oversized-extensions-v2.spz",
                    gsspz::diag::kImportLimitExceeded);
    TestReadFailure("sh-degree-5-v2.spz", gsspz::diag::kInvalidShDegree);
    TestReadFailure("count-exceeds-stream-v2.spz",
                    gsspz::diag::kTruncatedContainer);
    TestReadFailure("truncated-gzip-header.spz",
                    gsspz::diag::kMalformedContainer);
    TestReadFailure("short-stream.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("truncated-payload-v2.spz",
                    gsspz::diag::kTruncatedContainer);
    TestReadFailure("truncated-deflate-v2.spz",
                    gsspz::diag::kTruncatedContainer);
    TestReadFailure("corrupt-deflate.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("bad-crc-v2.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("bad-isize-v2.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("bad-fhcrc-v2.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("trailing-decompressed-v2.spz", gsspz::diag::kTrailingData);
    TestReadFailure("trailing-after-member-v2.spz", gsspz::diag::kTrailingData);
    TestReadFailure("does-not-exist.spz", gsspz::diag::kUnreadableFile);

    // Declared coordinate frames (SPZ_FORMAT.md §5), in both generations.
    TestReadFailure("coordinate-rdf-v4.spz",
                    gsspz::diag::kUnsupportedCoordinateSystem);
    TestReadFailure("coordinate-rdf-v2.spz",
                    gsspz::diag::kUnsupportedCoordinateSystem);
    TestReadFailure("coordinate-undefined-v4.spz",
                    gsspz::diag::kUnsupportedCoordinateSystem);
    TestReadFailure("coordinate-short-v4.spz",
                    gsspz::diag::kMalformedContainer);

    // The v4 container (ADR 0002 item 6 and its fixture list).
    TestReadFailure("gzip-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("truncated-header-v4.spz",
                    gsspz::diag::kTruncatedContainer);
    TestReadFailure("shared-limit-v4.spz", gsspz::diag::kImportLimitExceeded);
    TestReadFailure("count-exceeds-file-v4.spz",
                    gsspz::diag::kTruncatedContainer);
    TestReadFailure("unflagged-zone-v4.spz", gsspz::diag::kTrailingData);
    TestReadFailure("toc-offset-low-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("toc-offset-past-end-v4.spz",
                    gsspz::diag::kMalformedContainer);
    TestReadFailure("stream-count-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("stream-size-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("toc-short-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("toc-overlong-v4.spz", gsspz::diag::kTruncatedContainer);
    TestReadFailure("truncated-toc-v4.spz", gsspz::diag::kTruncatedContainer);
    TestReadFailure("truncated-frame-v4.spz", gsspz::diag::kTruncatedContainer);
    TestReadFailure("corrupt-frame-v4.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("corrupt-block-v4.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("bad-checksum-v4.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("second-frame-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("second-frame-last-v4.spz",
                    gsspz::diag::kMalformedContainer);
    TestReadFailure("skippable-outside-last-v4.spz",
                    gsspz::diag::kMalformedContainer);
    TestReadFailure("skippable-first-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("truncated-skippable-v4.spz",
                    gsspz::diag::kTruncatedContainer);
    TestReadFailure("content-size-v4.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("oversize-frame-v4.spz", gsspz::diag::kCorruptContainer);

    TestHeaderOnlySemantics();
    TestCanRead();
    TestOversizedSource();
    TestOversizedV4ExtensionZone();
    TestInternalMisuse();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
