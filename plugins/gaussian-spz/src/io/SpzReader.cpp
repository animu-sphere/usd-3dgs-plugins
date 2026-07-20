// SPDX-License-Identifier: Apache-2.0
#include "io/SpzReader.h"

#include "io/GaussianSpzDiagnostics.h"

#include "miniz.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace openstrata::gs::spz {
namespace {

// Little-endian encoding of the container magic 0x5053474e: "NGSP". In v1-v3
// it is the first field of the *decompressed* stream; the v4 container stores
// it in plaintext at file offset 0 (SPZ_FORMAT.md §4, §6).
constexpr unsigned char kMagicBytes[4] = {0x4e, 0x47, 0x53, 0x50};
constexpr std::uint32_t kMinSupportedVersion = 1;
constexpr std::uint32_t kMaxSupportedVersion = 3;
constexpr std::uint32_t kZstdContainerVersion = 4;
constexpr std::size_t kHeaderSize = 16;
// Specification bound on the header SH degree field. Degrees the shared model
// carries (0-3) are a semantic-decoder concern; the container accepts the
// documented range so a degree-4 file fails with a decoding decision, not a
// bogus malformed-container error.
constexpr std::uint32_t kSpecMaxShDegree = 4;
// The reference implementation rejects counts above INT32_MAX; mirroring it
// also keeps every size computation trivially inside 64 bits.
constexpr std::uint32_t kMaxPointCount = 0x7fffffff;
// DEFLATE cannot expand input by more than 1032:1 (258-byte match from a
// 2-bit code), so a header whose declared payload exceeds this bound over the
// available compressed bytes is provably truncated — rejected before the
// payload allocation, not discovered as an out-of-memory failure.
constexpr std::uint64_t kMaxDeflateExpansion = 1032;
// CanRead and metadata reads first try a bounded prefix of the file. If the
// gzip header or the 16 decompressed header bytes do not fit (oversized
// FEXTRA/FNAME fields, adversarial stored-block padding), they retry with the
// whole file rather than misreporting.
constexpr std::size_t kPrefixLimit = 64 * 1024;

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

std::uint32_t ReadLe32(const unsigned char* bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

bool LoadFile(
    const std::string& path,
    std::size_t limit,
    std::vector<unsigned char>* out,
    std::uint64_t* fileSize,
    std::string* error)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        SetError(error, diag::kUnreadableFile,
            "The file could not be opened for reading: " + path);
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size < 0) {
        SetError(error, diag::kUnreadableFile,
            "The file size could not be determined: " + path);
        return false;
    }
    *fileSize = static_cast<std::uint64_t>(size);
    const std::size_t want = static_cast<std::size_t>(
        std::min<std::uint64_t>(*fileSize, limit));
    out->resize(want);
    in.seekg(0);
    if (want != 0 &&
        !in.read(reinterpret_cast<char*>(out->data()),
                 static_cast<std::streamsize>(want))) {
        SetError(error, diag::kUnreadableFile,
            "The file could not be read: " + path);
        return false;
    }
    return true;
}

enum class Signature { PlaintextNgsp, Gzip, Unknown };

Signature DetectSignature(const std::vector<unsigned char>& data) noexcept
{
    if (data.size() >= 4 && std::equal(kMagicBytes, kMagicBytes + 4, data.data())) {
        return Signature::PlaintextNgsp;
    }
    if (data.size() >= 2 && data[0] == 0x1f && data[1] == 0x8b) {
        return Signature::Gzip;
    }
    return Signature::Unknown;
}

// A plaintext NGSP magic at offset 0 is the v4 (ZSTD) container layout, which
// this release defers; v1-v3 are always gzip-wrapped. Always an error, but a
// *specific* one: unsupported version for v4 and beyond, malformed for
// versions that never had a plaintext layout.
bool FailPlaintextContainer(
    const std::vector<unsigned char>& data, std::string* error)
{
    if (data.size() < 8) {
        SetError(error, diag::kMalformedContainer,
            "The file carries the plaintext SPZ magic but is shorter than a "
            "version field.");
        return false;
    }
    const std::uint32_t version = ReadLe32(data.data() + 4);
    if (version >= kZstdContainerVersion) {
        SetError(error, diag::kUnsupportedVersion,
            "SPZ version " + std::to_string(version) + " (the ZSTD container "
            "layout) is not supported by this release; supported container "
            "versions are 1-3. Version 4 support is planned for v0.5.0.");
    } else {
        SetError(error, diag::kMalformedContainer,
            "The file carries a plaintext SPZ magic with version " +
            std::to_string(version) + ", which does not match any documented "
            "layout: versions 1-3 are gzip-wrapped.");
    }
    return false;
}

enum class GzipParse { Ok, Malformed, NeedMore };

// RFC 1952 member header. `complete` says whether `data` is the whole file,
// turning "ran out of bytes" into malformed instead of retry-with-more.
GzipParse ParseGzipHeader(
    const std::vector<unsigned char>& data,
    bool complete,
    std::size_t* deflateOffset,
    std::string* error)
{
    const auto need = [&](std::size_t bytes) {
        return data.size() >= bytes;
    };
    const auto fail = [&](const std::string& message) {
        SetError(error, diag::kMalformedContainer, message);
        return GzipParse::Malformed;
    };
    const auto short_ = [&](const std::string& message) {
        return complete ? fail(message) : GzipParse::NeedMore;
    };

    if (!need(10)) {
        return short_("The gzip member header is truncated.");
    }
    if (data[2] != 0x08) {
        return fail("The gzip compression method " +
            std::to_string(static_cast<int>(data[2])) +
            " is not DEFLATE.");
    }
    const unsigned char flg = data[3];
    if ((flg & 0xe0) != 0) {
        return fail("Reserved gzip FLG bits are set.");
    }

    std::size_t pos = 10;
    if ((flg & 0x04) != 0) { // FEXTRA
        if (!need(pos + 2)) {
            return short_("The gzip FEXTRA length field is truncated.");
        }
        const std::size_t xlen =
            static_cast<std::size_t>(data[pos]) |
            (static_cast<std::size_t>(data[pos + 1]) << 8);
        pos += 2;
        if (!need(pos + xlen)) {
            return short_("The gzip FEXTRA field is truncated.");
        }
        pos += xlen;
    }
    for (const unsigned char nameFlag : {
             static_cast<unsigned char>(0x08),    // FNAME
             static_cast<unsigned char>(0x10)}) { // FCOMMENT
        if ((flg & nameFlag) == 0) {
            continue;
        }
        while (true) {
            if (pos >= data.size()) {
                return short_("A gzip header string field is unterminated.");
            }
            if (data[pos++] == 0x00) {
                break;
            }
        }
    }
    if ((flg & 0x02) != 0) { // FHCRC
        if (!need(pos + 2)) {
            return short_("The gzip header CRC field is truncated.");
        }
        pos += 2;
    }

    *deflateOffset = pos;
    return GzipParse::Ok;
}

// Streaming raw-DEFLATE decompression over an in-memory compressed span.
// Tracks consumed input, total output, and the CRC32 of everything produced
// so the gzip trailer can be verified without keeping a second copy of the
// decompressed stream.
class RawInflator {
public:
    enum class Status { Progress, End, Corrupt };

    ~RawInflator()
    {
        if (_initialized) {
            mz_inflateEnd(&_stream);
        }
    }

    bool Init(const unsigned char* data, std::size_t size) noexcept
    {
        _stream = mz_stream{};
        if (mz_inflateInit2(&_stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            return false;
        }
        _initialized = true;
        _in = data;
        _inSize = size;
        return true;
    }

    // Decompresses into `out` until `outCap` bytes are produced, the stream
    // ends, or input runs out. Progress with *produced < outCap therefore
    // means the compressed data ended before the stream did: truncated input.
    Status Pump(unsigned char* out, std::size_t outCap, std::size_t* produced)
    {
        constexpr std::size_t kChunk = std::size_t{1} << 30;
        *produced = 0;
        while (*produced < outCap && !_finished) {
            _stream.next_in = const_cast<unsigned char*>(_in + _inPos);
            _stream.avail_in = static_cast<mz_uint32>(
                std::min(_inSize - _inPos, kChunk));
            _stream.next_out = out + *produced;
            _stream.avail_out = static_cast<mz_uint32>(
                std::min(outCap - *produced, kChunk));

            const mz_uint32 availInBefore = _stream.avail_in;
            const mz_uint32 availOutBefore = _stream.avail_out;
            const int status = mz_inflate(&_stream, MZ_SYNC_FLUSH);
            const std::size_t consumed = availInBefore - _stream.avail_in;
            const std::size_t got = availOutBefore - _stream.avail_out;

            if (got != 0) {
                _crc = static_cast<std::uint32_t>(mz_crc32(
                    _crc, out + *produced, got));
            }
            _inPos += consumed;
            *produced += got;
            _totalOut += got;

            if (status == MZ_STREAM_END) {
                _finished = true;
                break;
            }
            if (status == MZ_DATA_ERROR) {
                return Status::Corrupt;
            }
            if (status != MZ_OK && status != MZ_BUF_ERROR) {
                return Status::Corrupt;
            }
            if (consumed == 0 && got == 0) {
                if (_inPos == _inSize) {
                    break; // input exhausted before the stream ended
                }
                return Status::Corrupt; // no progress despite input and space
            }
        }
        return _finished ? Status::End : Status::Progress;
    }

    bool Finished() const noexcept { return _finished; }
    bool InputExhausted() const noexcept { return _inPos == _inSize; }
    std::size_t ConsumedInput() const noexcept { return _inPos; }
    std::uint64_t TotalOutput() const noexcept { return _totalOut; }
    std::uint32_t Crc32() const noexcept { return _crc; }

private:
    mz_stream _stream{};
    bool _initialized = false;
    bool _finished = false;
    const unsigned char* _in = nullptr;
    std::size_t _inSize = 0;
    std::size_t _inPos = 0;
    std::uint64_t _totalOut = 0;
    std::uint32_t _crc = MZ_CRC32_INIT;
};

bool ValidateHeaderBytes(
    const unsigned char (&bytes)[kHeaderSize],
    SpzHeader* header,
    std::string* error)
{
    if (!std::equal(kMagicBytes, kMagicBytes + 4, bytes)) {
        SetError(error, diag::kNotSpzContainer,
            "The gzip member does not begin with the SPZ magic 'NGSP'.");
        return false;
    }
    header->version = ReadLe32(bytes + 4);
    header->pointCount = ReadLe32(bytes + 8);
    header->shDegree = bytes[12];
    header->fractionalBits = bytes[13];
    header->flags = bytes[14];
    header->reserved = bytes[15];

    if (header->version < kMinSupportedVersion ||
        header->version > kMaxSupportedVersion) {
        if (header->version >= kZstdContainerVersion) {
            SetError(error, diag::kUnsupportedVersion,
                "SPZ version " + std::to_string(header->version) +
                " is not supported by this release; supported container "
                "versions are 1-3. Version 4 (ZSTD) support is planned for "
                "v0.5.0.");
        } else {
            SetError(error, diag::kUnsupportedVersion,
                "SPZ version " + std::to_string(header->version) +
                " is not a defined container version; supported versions "
                "are 1-3.");
        }
        return false;
    }
    if (header->pointCount == 0) {
        SetError(error, diag::kEmptyPointSet,
            "The header declares zero Gaussians.");
        return false;
    }
    if (header->pointCount > kMaxPointCount) {
        SetError(error, diag::kInvalidPointCount,
            "The header declares " + std::to_string(header->pointCount) +
            " Gaussians, above the format maximum of " +
            std::to_string(kMaxPointCount) + ".");
        return false;
    }
    if (header->shDegree > kSpecMaxShDegree) {
        SetError(error, diag::kInvalidShDegree,
            "The header declares SH degree " +
            std::to_string(static_cast<int>(header->shDegree)) +
            ", outside the specification range 0-" +
            std::to_string(kSpecMaxShDegree) + ".");
        return false;
    }
    return true;
}

std::uint64_t ExpectedPayloadBytes(const SpzHeader& header) noexcept
{
    // Attribute-major stream: positions, alphas, colors, scales, rotations,
    // spherical harmonics. pointCount <= INT32_MAX and the per-point width is
    // at most 92 bytes (v3, degree 4), so the product stays far below 2^63.
    const std::uint64_t perPoint =
        static_cast<std::uint64_t>(header.BytesPerPosition()) +
        1 +                     // alpha
        3 +                     // color
        3 +                     // scale
        header.BytesPerRotation() +
        3 * static_cast<std::uint64_t>(header.ShDimensions());
    return perPoint * header.pointCount;
}

// The declared payload must be producible from the compressed bytes that are
// actually present. `compressedAvailable` excludes the gzip trailer.
bool CheckDeclaredSizePlausible(
    const SpzHeader& header,
    std::uint64_t compressedAvailable,
    std::string* error)
{
    const std::uint64_t expected = ExpectedPayloadBytes(header);
    if (expected > std::numeric_limits<std::size_t>::max() - kHeaderSize) {
        SetError(error, diag::kInvalidPointCount,
            "The declared payload of " + std::to_string(expected) +
            " bytes is not addressable on this platform.");
        return false;
    }
    if (kHeaderSize + expected > compressedAvailable * kMaxDeflateExpansion) {
        SetError(error, diag::kTruncatedContainer,
            "The header declares " + std::to_string(header.pointCount) +
            " Gaussians (" + std::to_string(expected) + " payload bytes), "
            "but the compressed stream is too small to contain them; the "
            "file is truncated.");
        return false;
    }
    return true;
}

// Decompresses exactly the 16 header bytes and validates them. Returns
// NeedMore when `complete` is false and the compressed prefix ran out before
// the header materialized.
GzipParse ReadHeaderFromBuffer(
    const std::vector<unsigned char>& data,
    bool complete,
    std::uint64_t fileSize,
    SpzHeader* header,
    std::string* error)
{
    std::size_t deflateOffset = 0;
    const GzipParse framing =
        ParseGzipHeader(data, complete, &deflateOffset, error);
    if (framing != GzipParse::Ok) {
        return framing;
    }

    RawInflator inflator;
    if (!inflator.Init(data.data() + deflateOffset,
                       data.size() - deflateOffset)) {
        SetError(error, diag::kInternalError,
            "The DEFLATE decompressor could not be initialized.");
        return GzipParse::Malformed;
    }

    unsigned char headerBytes[kHeaderSize] = {};
    std::size_t produced = 0;
    const RawInflator::Status status =
        inflator.Pump(headerBytes, kHeaderSize, &produced);
    if (status == RawInflator::Status::Corrupt) {
        SetError(error, diag::kCorruptContainer,
            "The DEFLATE stream is not decodable.");
        return GzipParse::Malformed;
    }
    if (produced < kHeaderSize) {
        if (status == RawInflator::Status::End) {
            SetError(error, diag::kMalformedContainer,
                "The decompressed stream is shorter than the 16-byte SPZ "
                "header.");
            return GzipParse::Malformed;
        }
        if (!complete) {
            return GzipParse::NeedMore;
        }
        SetError(error, diag::kTruncatedContainer,
            "The compressed stream ends before the 16-byte SPZ header is "
            "complete.");
        return GzipParse::Malformed;
    }

    if (!ValidateHeaderBytes(headerBytes, header, error)) {
        return GzipParse::Malformed;
    }

    // Trailer bytes are not decompressed input; require room for them.
    if (fileSize < deflateOffset + 8) {
        SetError(error, diag::kTruncatedContainer,
            "The file is too small to hold the gzip trailer.");
        return GzipParse::Malformed;
    }
    if (!CheckDeclaredSizePlausible(
            *header, fileSize - deflateOffset - 8, error)) {
        return GzipParse::Malformed;
    }
    return GzipParse::Ok;
}

// Checks only the container signature: the plaintext NGSP magic, or a gzip
// member whose decompressed stream begins with the magic. Header fields are
// deliberately not validated: an unsupported version, zero count, or invalid
// SH degree is an SPZ file whose problem Read() must explain with its
// specific diagnostic, not one this bundle silently disowns (§7.6).
bool CanReadBuffer(const std::vector<unsigned char>& data, bool complete)
{
    std::size_t deflateOffset = 0;
    if (ParseGzipHeader(data, complete, &deflateOffset, nullptr) !=
        GzipParse::Ok) {
        // NeedMore surfaces to the caller through the complete=true retry.
        return false;
    }
    RawInflator inflator;
    if (!inflator.Init(data.data() + deflateOffset,
                       data.size() - deflateOffset)) {
        return false;
    }
    unsigned char magic[4] = {};
    std::size_t produced = 0;
    if (inflator.Pump(magic, sizeof magic, &produced) ==
        RawInflator::Status::Corrupt) {
        return false;
    }
    return produced == sizeof magic &&
           std::equal(kMagicBytes, kMagicBytes + 4, magic);
}

} // namespace

bool SpzReader::CanRead(const std::string& path) const noexcept
{
    try {
        std::vector<unsigned char> data;
        std::uint64_t fileSize = 0;
        if (!LoadFile(path, kPrefixLimit, &data, &fileSize, nullptr)) {
            return false;
        }
        switch (DetectSignature(data)) {
        case Signature::PlaintextNgsp:
            // The v4-and-later layout. Claimed so Read() can report the
            // specific unsupported-version diagnostic instead of USD
            // reporting that no plugin was found.
            return true;
        case Signature::Unknown:
            return false;
        case Signature::Gzip:
            break;
        }

        if (CanReadBuffer(data, data.size() >= fileSize)) {
            return true;
        }
        if (data.size() >= fileSize) {
            return false;
        }
        // The prefix was inconclusive (oversized gzip header fields or
        // stored-block padding before the magic); retry with the whole file.
        if (!LoadFile(path, std::numeric_limits<std::size_t>::max(),
                      &data, &fileSize, nullptr)) {
            return false;
        }
        return CanReadBuffer(data, true);
    } catch (...) {
        return false;
    }
}

bool SpzReader::ReadHeader(
    const std::string& path,
    SpzHeader* header,
    std::string* error) const
{
    if (!header) {
        SetError(error, diag::kInternalError,
            "SpzReader received a null header output.");
        return false;
    }

    std::vector<unsigned char> data;
    std::uint64_t fileSize = 0;
    if (!LoadFile(path, kPrefixLimit, &data, &fileSize, error)) {
        return false;
    }
    switch (DetectSignature(data)) {
    case Signature::PlaintextNgsp:
        return FailPlaintextContainer(data, error);
    case Signature::Unknown:
        SetError(error, diag::kNotSpzContainer,
            "The file has neither the gzip signature of an SPZ v1-v3 "
            "container nor a plaintext SPZ magic.");
        return false;
    case Signature::Gzip:
        break;
    }

    GzipParse result = ReadHeaderFromBuffer(
        data, data.size() >= fileSize, fileSize, header, error);
    if (result == GzipParse::NeedMore) {
        if (!LoadFile(path, std::numeric_limits<std::size_t>::max(),
                      &data, &fileSize, error)) {
            return false;
        }
        result = ReadHeaderFromBuffer(data, true, fileSize, header, error);
    }
    return result == GzipParse::Ok;
}

bool SpzReader::Read(
    const std::string& path,
    SpzPackedDocument* document,
    std::string* error) const
{
    if (!document) {
        SetError(error, diag::kInternalError,
            "SpzReader received a null document output.");
        return false;
    }

    std::vector<unsigned char> file;
    std::uint64_t fileSize = 0;
    if (!LoadFile(path, std::numeric_limits<std::size_t>::max(),
                  &file, &fileSize, error)) {
        return false;
    }
    switch (DetectSignature(file)) {
    case Signature::PlaintextNgsp:
        return FailPlaintextContainer(file, error);
    case Signature::Unknown:
        SetError(error, diag::kNotSpzContainer,
            "The file has neither the gzip signature of an SPZ v1-v3 "
            "container nor a plaintext SPZ magic.");
        return false;
    case Signature::Gzip:
        break;
    }

    std::size_t deflateOffset = 0;
    if (ParseGzipHeader(file, true, &deflateOffset, error) != GzipParse::Ok) {
        return false;
    }
    if (file.size() < deflateOffset + 8) {
        SetError(error, diag::kTruncatedContainer,
            "The file is too small to hold the gzip trailer.");
        return false;
    }

    RawInflator inflator;
    if (!inflator.Init(file.data() + deflateOffset,
                       file.size() - deflateOffset)) {
        SetError(error, diag::kInternalError,
            "The DEFLATE decompressor could not be initialized.");
        return false;
    }

    // 1. The 16-byte header.
    unsigned char headerBytes[kHeaderSize] = {};
    std::size_t produced = 0;
    RawInflator::Status status =
        inflator.Pump(headerBytes, kHeaderSize, &produced);
    if (status == RawInflator::Status::Corrupt) {
        SetError(error, diag::kCorruptContainer,
            "The DEFLATE stream is not decodable.");
        return false;
    }
    if (produced < kHeaderSize) {
        if (status == RawInflator::Status::End) {
            SetError(error, diag::kMalformedContainer,
                "The decompressed stream is shorter than the 16-byte SPZ "
                "header.");
        } else {
            SetError(error, diag::kTruncatedContainer,
                "The compressed stream ends before the 16-byte SPZ header "
                "is complete.");
        }
        return false;
    }

    SpzHeader header;
    if (!ValidateHeaderBytes(headerBytes, &header, error)) {
        return false;
    }
    if (!CheckDeclaredSizePlausible(
            header, file.size() - deflateOffset - 8, error)) {
        return false;
    }

    // 2. The attribute-major payload, decompressed straight into its final
    // buffer. The plausibility bound above caps the allocation by what the
    // compressed bytes could actually produce.
    const std::size_t expected =
        static_cast<std::size_t>(ExpectedPayloadBytes(header));
    document->payload.assign(expected, 0);
    document->extensions.clear();

    status = inflator.Pump(document->payload.data(), expected, &produced);
    if (status == RawInflator::Status::Corrupt) {
        SetError(error, diag::kCorruptContainer,
            "The DEFLATE stream is not decodable.");
        return false;
    }
    if (produced < expected) {
        SetError(error, diag::kTruncatedContainer,
            "The attribute streams end after " + std::to_string(produced) +
            " of the " + std::to_string(expected) + " bytes the header "
            "declares.");
        return false;
    }

    // 3. Whatever follows the documented streams: extension records when the
    // header announces them, otherwise a malformed file.
    std::uint64_t undeclaredTrailing = 0;
    while (!inflator.Finished()) {
        unsigned char scratch[4096];
        status = inflator.Pump(scratch, sizeof scratch, &produced);
        if (status == RawInflator::Status::Corrupt) {
            SetError(error, diag::kCorruptContainer,
                "The DEFLATE stream is not decodable.");
            return false;
        }
        if (header.HasExtensions()) {
            document->extensions.insert(
                document->extensions.end(), scratch, scratch + produced);
        } else {
            undeclaredTrailing += produced;
        }
        if (status == RawInflator::Status::Progress && produced == 0) {
            SetError(error, diag::kTruncatedContainer,
                "The compressed stream ends without a DEFLATE terminator.");
            return false;
        }
    }
    if (undeclaredTrailing != 0) {
        SetError(error, diag::kTrailingData,
            std::to_string(undeclaredTrailing) + " decompressed bytes follow "
            "the attribute streams, but the header does not declare "
            "extensions.");
        return false;
    }

    // 4. The gzip trailer: CRC32 and length of the decompressed stream.
    const std::size_t trailerOffset = deflateOffset + inflator.ConsumedInput();
    if (file.size() < trailerOffset + 8) {
        SetError(error, diag::kTruncatedContainer,
            "The gzip trailer is truncated.");
        return false;
    }
    const std::uint32_t storedCrc = ReadLe32(file.data() + trailerOffset);
    const std::uint32_t storedSize = ReadLe32(file.data() + trailerOffset + 4);
    if (storedCrc != inflator.Crc32()) {
        SetError(error, diag::kCorruptContainer,
            "The gzip CRC32 does not match the decompressed stream.");
        return false;
    }
    if (storedSize !=
        static_cast<std::uint32_t>(inflator.TotalOutput() & 0xffffffffu)) {
        SetError(error, diag::kCorruptContainer,
            "The gzip length field does not match the decompressed stream.");
        return false;
    }
    if (file.size() > trailerOffset + 8) {
        SetError(error, diag::kTrailingData,
            std::to_string(file.size() - trailerOffset - 8) + " bytes follow "
            "the gzip member.");
        return false;
    }

    // 5. Fixed attribute-major slicing (SPZ_FORMAT.md §4).
    const std::size_t count = header.pointCount;
    std::size_t cursor = 0;
    const auto slice = [&](std::size_t size) {
        const SpzPackedDocument::Span span{cursor, size};
        cursor += size;
        return span;
    };
    document->header = header;
    document->positions = slice(count * header.BytesPerPosition());
    document->alphas = slice(count);
    document->colors = slice(count * 3);
    document->scales = slice(count * 3);
    document->rotations = slice(count * header.BytesPerRotation());
    document->sh = slice(count * 3 * header.ShDimensions());
    return true;
}

} // namespace openstrata::gs::spz
