// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSplatDiagnostics.h"
#include "io/SplatReader.h"
#include "openstrata/gs/GaussianImportLimits.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace gs = openstrata::gs;
namespace gssplat = openstrata::gs::splat;

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
    return (std::filesystem::path(GAUSSIAN_SPLAT_FIXTURE_DIR) / name).string();
}

bool HasCode(const std::string& error, const char* code)
{
    return error.rfind(std::string("[") + code + "]", 0) == 0;
}

std::vector<unsigned char> FileBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
}

// The reader hands back the file's bytes unchanged: it interprets no record.
void TestReadWholeRecords()
{
    const gssplat::SplatReader reader;
    gssplat::SplatDocument document;
    std::string error;
    CHECK(reader.Read(Fixture("decode-known.splat"), &document, &error));
    CHECK(error.empty());
    CHECK(document.layout.fileBytes == 64);
    CHECK(document.layout.recordCount == 2);
    CHECK(document.records == FileBytes(Fixture("decode-known.splat")));

    gssplat::SplatLayout layout;
    CHECK(reader.ReadLayout(Fixture("minimal-one.splat"), &layout, &error));
    CHECK(layout.fileBytes == 32);
    CHECK(layout.recordCount == 1);
}

// Record values are the decoder's concern. A file whose values the decoder
// rejects still has a valid layout.
void TestValuesAreNotInterpreted()
{
    const gssplat::SplatReader reader;
    for (const char* name :
         {"nonfinite-position.splat", "infinite-position.splat",
          "zero-scale.splat", "negative-scale.splat", "nonfinite-scale.splat",
          "zero-rotation.splat"}) {
        gssplat::SplatDocument document;
        std::string error;
        if (!reader.Read(Fixture(name), &document, &error)) {
            std::cerr << name << ": " << error << "\n";
            ++failures;
        }
    }
}

void TestReadFailure(const char* name, const char* code)
{
    const gssplat::SplatReader reader;
    std::string error;

    gssplat::SplatLayout layout;
    CHECK(!reader.ReadLayout(Fixture(name), &layout, &error));
    if (!HasCode(error, code)) {
        std::cerr << name << ": expected " << code << " from ReadLayout, got '"
                  << error << "'\n";
        ++failures;
    }

    error.clear();
    gssplat::SplatDocument document;
    CHECK(!reader.Read(Fixture(name), &document, &error));
    if (!HasCode(error, code)) {
        std::cerr << name << ": expected " << code << " from Read, got '"
                  << error << "'\n";
        ++failures;
    }
}

// The routing gate looks only at the size (SPLAT_FORMAT.md §4): a renamed
// file of whole records passes it and is named in Read().
void TestCanRead()
{
    const gssplat::SplatReader reader;
    CHECK(reader.CanRead(Fixture("decode-known.splat")));
    CHECK(reader.CanRead(Fixture("minimal-one.splat")));
    CHECK(reader.CanRead(Fixture("nonfinite-position.splat")));
    CHECK(reader.CanRead(Fixture("renamed-ply.splat")));
    CHECK(!reader.CanRead(Fixture("empty.splat")));
    CHECK(!reader.CanRead(Fixture("partial-record.splat")));
    CHECK(!reader.CanRead(Fixture("renamed-ply-odd-size.splat")));
    CHECK(!reader.CanRead(Fixture("does-not-exist.splat")));
    CHECK(!reader.CanRead(GAUSSIAN_SPLAT_FIXTURE_DIR));
}

// The shared count ceiling (IMPORT_LIMITS.md), from the size alone. The file
// is grown with resize_file, which does not write the extension (sparse on
// Linux and macOS, an unwritten extension on NTFS), and both reads must
// refuse it before any bulk read.
void TestImportLimit()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        "gaussian-splat-reader-import-limit";
    std::filesystem::create_directories(directory);
    const std::filesystem::path path = directory / "limit.splat";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const unsigned char record[gssplat::kRecordBytes] = {};
        out.write(reinterpret_cast<const char*>(record), sizeof record);
        CHECK(static_cast<bool>(out));
    }

    const gssplat::SplatReader reader;
    const std::uint64_t atLimit =
        static_cast<std::uint64_t>(gs::kMaxGaussianCount) *
        gssplat::kRecordBytes;

    std::error_code grown;
    std::filesystem::resize_file(path, atLimit, grown);
    CHECK(!grown);
    gssplat::SplatLayout layout;
    std::string error;
    CHECK(reader.ReadLayout(path.string(), &layout, &error));
    CHECK(layout.recordCount == gs::kMaxGaussianCount);

    std::filesystem::resize_file(path, atLimit + gssplat::kRecordBytes, grown);
    CHECK(!grown);
    error.clear();
    CHECK(!reader.ReadLayout(path.string(), &layout, &error));
    CHECK(HasCode(error, gssplat::diag::kImportLimitExceeded));
    error.clear();
    gssplat::SplatDocument document;
    CHECK(!reader.Read(path.string(), &document, &error));
    CHECK(HasCode(error, gssplat::diag::kImportLimitExceeded));
    CHECK(document.records.empty());

    std::error_code removed;
    std::filesystem::remove_all(directory, removed);
}

void TestInternalMisuse()
{
    const gssplat::SplatReader reader;
    std::string error;
    CHECK(!reader.ReadLayout(Fixture("minimal-one.splat"), nullptr, &error));
    CHECK(HasCode(error, gssplat::diag::kInternalError));
    error.clear();
    CHECK(!reader.Read(Fixture("minimal-one.splat"), nullptr, &error));
    CHECK(HasCode(error, gssplat::diag::kInternalError));
}

} // namespace

int main()
{
    TestReadWholeRecords();
    TestValuesAreNotInterpreted();

    TestReadFailure("empty.splat", gssplat::diag::kEmptyFile);
    TestReadFailure("partial-record.splat", gssplat::diag::kPartialRecord);
    TestReadFailure("renamed-ply.splat", gssplat::diag::kForeignFormat);
    TestReadFailure("renamed-ply-crlf.splat", gssplat::diag::kForeignFormat);
    TestReadFailure("renamed-ply-odd-size.splat",
                    gssplat::diag::kForeignFormat);
    TestReadFailure("renamed-spz-v4.splat", gssplat::diag::kForeignFormat);
    TestReadFailure("renamed-sog.splat", gssplat::diag::kForeignFormat);
    TestReadFailure("renamed-glb.splat", gssplat::diag::kForeignFormat);
    TestReadFailure("does-not-exist.splat", gssplat::diag::kUnreadableFile);

    TestCanRead();
    TestImportLimit();
    TestInternalMisuse();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
