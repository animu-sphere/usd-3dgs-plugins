// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSpzDiagnostics.h"
#include "io/SpzReader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace gsspz = openstrata::gs::spz;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

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

bool SpanEquals(
    const gsspz::SpzPackedDocument::Span& span,
    std::size_t offset,
    std::size_t size)
{
    return span.offset == offset && span.size == size;
}

// A valid single-point degree-0 container in each supported version. The
// versions differ only in per-point widths: v1 stores float16 positions,
// v3 four-byte smallest-three rotations.
void TestMinimalContainer(
    const char* fixture,
    std::uint32_t version,
    std::size_t positionBytes,
    std::size_t rotationBytes)
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
    CHECK(reader.Read(
        Fixture("three-points-degree1-v2.spz"), &document, &error));
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
void TestHighDegreeShSizing(const char* fixture, int degree, std::size_t shBytes)
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
    CHECK(std::string(document.extensions.begin(), document.extensions.end())
          == "EXTBYTES");
}

void TestReadFailure(const char* fixture, const char* code)
{
    const gsspz::SpzReader reader;
    gsspz::SpzPackedDocument document;
    std::string error;
    CHECK(!reader.Read(Fixture(fixture), &document, &error));
    if (!HasCode(error, code)) {
        std::cerr << fixture << ": expected " << code << ", got: "
                  << error << "\n";
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

    CHECK(reader.ReadHeader(
        Fixture("truncated-payload-v2.spz"), &header, &error));
    CHECK(header.pointCount == 3);
    CHECK(reader.ReadHeader(Fixture("bad-crc-v2.spz"), &header, &error));

    // Header-level defects fail with the same codes as the full read.
    CHECK(!reader.ReadHeader(Fixture("version-5.spz"), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kUnsupportedVersion));
    CHECK(!reader.ReadHeader(Fixture("empty-points-v2.spz"), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kEmptyPointSet));
    CHECK(!reader.ReadHeader(
        Fixture("count-exceeds-stream-v2.spz"), &header, &error));
    CHECK(HasCode(error, gsspz::diag::kTruncatedContainer));
}

// CanRead is signature-only (§7.6): every structurally identifiable SPZ
// container is claimed — including unsupported versions and damaged bodies,
// which Read() must explain with a specific code — while non-SPZ bytes and
// unreadable gzip are declined.
void TestCanRead()
{
    const gsspz::SpzReader reader;

    CHECK(reader.CanRead(Fixture("plaintext-v4.spz")));
    CHECK(reader.CanRead(Fixture("plaintext-v2.spz")));
    CHECK(reader.CanRead(Fixture("version-0.spz")));
    CHECK(reader.CanRead(Fixture("version-5.spz")));
    CHECK(reader.CanRead(Fixture("empty-points-v2.spz")));
    CHECK(reader.CanRead(Fixture("huge-count-v2.spz")));
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
    TestMultiPointShLayout();
    TestHighDegreeShSizing("degree3-v2.spz", 3, 45);
    TestHighDegreeShSizing("degree4-v2.spz", 4, 72);
    TestGzipHeaderFields();
    TestExtensionsPreserved();

    TestReadFailure("not-spz.spz", gsspz::diag::kNotSpzContainer);
    TestReadFailure("gzip-not-spz.spz", gsspz::diag::kNotSpzContainer);
    TestReadFailure("plaintext-v4.spz", gsspz::diag::kUnsupportedVersion);
    TestReadFailure("plaintext-v2.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("version-0.spz", gsspz::diag::kUnsupportedVersion);
    TestReadFailure("version-5.spz", gsspz::diag::kUnsupportedVersion);
    TestReadFailure("empty-points-v2.spz", gsspz::diag::kEmptyPointSet);
    TestReadFailure("huge-count-v2.spz", gsspz::diag::kInvalidPointCount);
    TestReadFailure("sh-degree-5-v2.spz", gsspz::diag::kInvalidShDegree);
    TestReadFailure(
        "count-exceeds-stream-v2.spz", gsspz::diag::kTruncatedContainer);
    TestReadFailure(
        "truncated-gzip-header.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure("short-stream.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure(
        "truncated-payload-v2.spz", gsspz::diag::kTruncatedContainer);
    TestReadFailure(
        "truncated-deflate-v2.spz", gsspz::diag::kTruncatedContainer);
    TestReadFailure("corrupt-deflate.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("bad-crc-v2.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("bad-isize-v2.spz", gsspz::diag::kCorruptContainer);
    TestReadFailure("bad-fhcrc-v2.spz", gsspz::diag::kMalformedContainer);
    TestReadFailure(
        "trailing-decompressed-v2.spz", gsspz::diag::kTrailingData);
    TestReadFailure(
        "trailing-after-member-v2.spz", gsspz::diag::kTrailingData);
    TestReadFailure("does-not-exist.spz", gsspz::diag::kUnreadableFile);

    TestHeaderOnlySemantics();
    TestCanRead();
    TestInternalMisuse();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
