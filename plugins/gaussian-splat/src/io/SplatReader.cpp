// SPDX-License-Identifier: Apache-2.0
#include "io/SplatReader.h"

#include "io/GaussianSplatDiagnostics.h"
#include "openstrata/gs/GaussianFileIO.h"
#include "openstrata/gs/GaussianImportLimits.h"
#include "openstrata/gs/GaussianSizeMath.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <utility>

namespace openstrata::gs::splat {
namespace {

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

// Formats whose files are commonly renamed to `.splat`, identified by a
// four-byte signature. A genuine `.splat` file starts with the float32 x
// coordinate of its first Gaussian, which matches one of these only at
// exactly one bit pattern each (SPLAT_FORMAT.md §4). The two-byte gzip
// signature of SPZ v1-v3 is too short to reject on.
struct Signature {
    unsigned char bytes[4];
    const char* format;
};

constexpr Signature kForeignSignatures[] = {
    {{'p', 'l', 'y', '\n'}, "PLY"},
    {{'p', 'l', 'y', '\r'}, "PLY"},
    {{'N', 'G', 'S', 'P'}, "SPZ version 4"},
    {{'P', 'K', 0x03, 0x04}, "ZIP (a bundled SOG archive)"},
    {{'g', 'l', 'T', 'F'}, "binary glTF (GLB)"},
};

// The size of a regular file, or false when it cannot be determined. Sized
// through the same UTF-8 path conversion the stream open uses.
bool RegularFileSize(const std::string& path, std::uint64_t* size) noexcept
{
    std::error_code error;
    const std::filesystem::path native = openstrata::gs::PathFromUtf8(path);
    if (!std::filesystem::is_regular_file(native, error) || error) {
        return false;
    }
    const std::uintmax_t bytes = std::filesystem::file_size(native, error);
    if (error) {
        return false;
    }
    *size = static_cast<std::uint64_t>(bytes);
    return true;
}

} // namespace

bool SplatReader::CanRead(const std::string& path) const noexcept
{
    try {
        std::uint64_t size = 0;
        return RegularFileSize(path, &size) && size != 0 &&
               size % kRecordBytes == 0;
    } catch (...) {
        return false;
    }
}

bool SplatReader::ReadLayout(const std::string& path, SplatLayout* layout,
                             std::string* error) const
{
    if (!layout) {
        SetError(error, diag::kInternalError,
                 "The .splat reader received a null layout output.");
        return false;
    }

    std::uint64_t size = 0;
    if (!RegularFileSize(path, &size)) {
        SetError(error, diag::kUnreadableFile,
                 "The file does not exist or is not a regular file.");
        return false;
    }
    if (size == 0) {
        SetError(error, diag::kEmptyFile,
                 "The file is empty; a .splat file holds at least one "
                 "32-byte record.");
        return false;
    }

    // The signature is checked before the size so that a renamed file of
    // another format is named as such, whatever its length.
    if (size >= 4) {
        std::ifstream in = openstrata::gs::OpenInputFile(path);
        unsigned char prefix[4] = {};
        if (!in || !in.read(reinterpret_cast<char*>(prefix), sizeof prefix)) {
            SetError(error, diag::kUnreadableFile,
                     "The first bytes of the file could not be read.");
            return false;
        }
        for (const Signature& signature : kForeignSignatures) {
            if (std::memcmp(prefix, signature.bytes, sizeof prefix) == 0) {
                SetError(error, diag::kForeignFormat,
                         std::string("The file starts with the ") +
                             signature.format +
                             " signature, so it is not a .splat file. It may "
                             "have been renamed; open it with its original "
                             "extension.");
                return false;
            }
        }
    }

    if (size % kRecordBytes != 0) {
        SetError(error, diag::kPartialRecord,
                 "The file size, " + std::to_string(size) +
                     " bytes, is not a multiple of the 32-byte record size; "
                     "the last " +
                     std::to_string(size % kRecordBytes) +
                     " byte(s) do not form a whole record.");
        return false;
    }

    // Compared in 64 bits before narrowing, so a 32-bit platform cannot wrap
    // the count of a huge file into range.
    const std::uint64_t count = size / kRecordBytes;
    if (count > static_cast<std::uint64_t>(kMaxGaussianCount)) {
        SetError(error, diag::kImportLimitExceeded,
                 "The file holds " + std::to_string(count) +
                     " records, more than the import limit of " +
                     std::to_string(kMaxGaussianCount) + " Gaussians.");
        return false;
    }

    layout->fileBytes = size;
    layout->recordCount = static_cast<std::size_t>(count);
    return true;
}

bool SplatReader::Read(const std::string& path, SplatDocument* document,
                       std::string* error) const
{
    if (!document) {
        SetError(error, diag::kInternalError,
                 "The .splat reader received a null document output.");
        return false;
    }

    SplatLayout layout;
    if (!ReadLayout(path, &layout, error)) {
        return false;
    }

    // The count is within the shared ceiling, so the byte size cannot
    // overflow; the checked helper keeps that true if the ceiling moves.
    std::size_t bytes = 0;
    std::vector<unsigned char> records;
    if (!CheckedMulSize(layout.recordCount, kRecordBytes, &bytes) ||
        !TryResize(&records, bytes)) {
        SetError(error, diag::kModelAllocationFailed,
                 "The record buffer for " + std::to_string(layout.recordCount) +
                     " Gaussians could not be allocated.");
        return false;
    }

    std::ifstream in = openstrata::gs::OpenInputFile(path);
    if (!in || !in.read(reinterpret_cast<char*>(records.data()),
                        static_cast<std::streamsize>(bytes))) {
        SetError(error, diag::kUnreadableFile,
                 "The records could not be read completely; the file may have "
                 "changed while it was being read.");
        return false;
    }

    document->layout = layout;
    document->records = std::move(records);
    return true;
}

} // namespace openstrata::gs::splat
