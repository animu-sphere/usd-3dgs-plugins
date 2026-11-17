// SPDX-License-Identifier: Apache-2.0
#include "io/SpzReader.h"

#include "io/GaussianSpzDiagnostics.h"
#include "openstrata/gs/GaussianFileIO.h"
#include "openstrata/gs/GaussianImportLimits.h"
#include "openstrata/gs/GaussianSizeMath.h"

#include "miniz.h"

// The static-only part of the zstd API (ZSTD_isSkippableFrame,
// ZSTD_decompressBound) is safe to use: the decoder is vendored at one pinned
// version and linked statically (ADR 0002).
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "zstd_errors.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace openstrata::gs::spz {
namespace {

// Little-endian encoding of the container magic 0x5053474e: "NGSP". In v1-v3
// it is the first field of the *decompressed* stream; the v4 container stores
// it in plaintext at file offset 0 (SPZ_FORMAT.md §4, §6).
constexpr unsigned char kMagicBytes[4] = {0x4e, 0x47, 0x53, 0x50};
constexpr std::uint32_t kMinSupportedVersion = 1;
constexpr std::uint32_t kMaxGzipVersion = 3;
constexpr std::uint32_t kZstdContainerVersion = 4;
constexpr std::uint32_t kMaxSupportedVersion = kZstdContainerVersion;
constexpr std::size_t kHeaderSize = 16;
// The v4 plaintext header: the 16 bytes of v1-v3 with the reserved byte
// replaced by numStreams, then tocByteOffset and twelve reserved bytes.
constexpr std::size_t kV4HeaderSize = 32;
constexpr std::size_t kTocEntryBytes = 16;
// Positions, alphas, colours, scales, rotations, spherical harmonics.
constexpr std::size_t kMaxStreams = 6;
// The reference reader's ceiling on the declared count against the file size:
// no v4 file decodes to more than 1024 bytes of positions per file byte.
constexpr std::uint64_t kV4MaxCompressionRatio = 1024;
// Extension records are bounded before they are buffered.
constexpr std::size_t kMaxExtensionBytes = 64 * 1024 * 1024;
// SPZ_ADOBE_coordinate_system values that mean the model frame. 0 is
// UNSPECIFIED, which the reference reader resolves to RUB.
constexpr std::uint32_t kCoordinateUnspecified = 0;
constexpr std::uint32_t kCoordinateRub = 4;
constexpr std::uint32_t kMaxCoordinateSystem = 16;
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
// FEXTRA/FNAME fields, adversarial stored-block padding), ReadHeader retries
// with the whole file rather than misreporting; CanRead retries with the
// larger bound below, because routing runs against arbitrary files and must
// never read an unbounded amount of them.
constexpr std::size_t kPrefixLimit = 64 * 1024;
// A genuine SPZ materializes its magic within a few dozen compressed bytes;
// only pathological gzip header fields push it out further. One mebibyte is
// orders of magnitude beyond any of those seen in practice, so a file whose
// magic is still not reachable within it is declined by CanRead rather than
// decompressed in full during format resolution.
constexpr std::size_t kCanReadRetryLimit = 1024 * 1024;
// The shared Gaussian ceiling keeps valid payloads below this bound, while a
// source-file cap prevents a small-count container with arbitrary trailing
// data from forcing the reader to materialize an unbounded input file.
constexpr std::uint64_t kMaxSpzFileBytes = 1ull * 1024ull * 1024ull * 1024ull;

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

std::uint64_t ReadLe64(const unsigned char* bytes) noexcept
{
    return static_cast<std::uint64_t>(ReadLe32(bytes)) |
           (static_cast<std::uint64_t>(ReadLe32(bytes + 4)) << 32);
}

std::string Hex32(std::uint32_t value)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text += kDigits[(value >> shift) & 0xfu];
    }
    return text;
}

bool LoadFile(const std::string& path, std::size_t limit,
              std::vector<unsigned char>* out, std::uint64_t* fileSize,
              std::string* error)
{
    std::ifstream in =
        openstrata::gs::OpenInputFile(path, std::ios::binary | std::ios::ate);
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
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(*fileSize, limit));
    out->resize(want);
    in.seekg(0);
    if (want != 0 && !in.read(reinterpret_cast<char*>(out->data()),
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
    if (data.size() >= 4 &&
        std::equal(kMagicBytes, kMagicBytes + 4, data.data())) {
        return Signature::PlaintextNgsp;
    }
    if (data.size() >= 2 && data[0] == 0x1f && data[1] == 0x8b) {
        return Signature::Gzip;
    }
    return Signature::Unknown;
}

// A plaintext NGSP magic at offset 0 is the zstd container layout, which
// begins with version 4. v1-v3 are always gzip-wrapped, so a plaintext header
// with a lower version matches no documented layout (malformed), and one with
// a higher version is a container this release does not know (unsupported).
bool CheckPlaintextVersion(const std::vector<unsigned char>& data,
                           std::string* error)
{
    if (data.size() < 8) {
        SetError(
            error, diag::kMalformedContainer,
            "The file carries the plaintext SPZ magic but is shorter than a "
            "version field.");
        return false;
    }
    const std::uint32_t version = ReadLe32(data.data() + 4);
    if (version == kZstdContainerVersion) {
        return true;
    }
    if (version > kZstdContainerVersion) {
        SetError(error, diag::kUnsupportedVersion,
                 "SPZ version " + std::to_string(version) +
                     " is not supported by this release; supported container "
                     "versions are 1-4.");
    } else {
        SetError(error, diag::kMalformedContainer,
                 "The file carries a plaintext SPZ magic with version " +
                     std::to_string(version) +
                     ", which does not match any documented "
                     "layout: versions 1-3 are gzip-wrapped.");
    }
    return false;
}

enum class ParseResult { Ok, Malformed, NeedMore };

// RFC 1952 member header. `complete` says whether `data` is the whole file,
// turning "ran out of bytes" into malformed instead of retry-with-more.
ParseResult ParseGzipHeader(const std::vector<unsigned char>& data,
                            bool complete, std::size_t* deflateOffset,
                            std::string* error)
{
    const auto need = [&](std::size_t bytes) { return data.size() >= bytes; };
    const auto fail = [&](const std::string& message) {
        SetError(error, diag::kMalformedContainer, message);
        return ParseResult::Malformed;
    };
    const auto short_ = [&](const std::string& message) {
        return complete ? fail(message) : ParseResult::NeedMore;
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
        const std::size_t xlen = static_cast<std::size_t>(data[pos]) |
                                 (static_cast<std::size_t>(data[pos + 1]) << 8);
        pos += 2;
        if (!need(pos + xlen)) {
            return short_("The gzip FEXTRA field is truncated.");
        }
        pos += xlen;
    }
    for (const unsigned char nameFlag :
         {static_cast<unsigned char>(0x08),    // FNAME
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
        // RFC 1952: the two least significant bytes of the CRC32 of the
        // header bytes preceding this field.
        const std::uint32_t computed = static_cast<std::uint32_t>(mz_crc32(
                                           MZ_CRC32_INIT, data.data(), pos)) &
                                       0xffffu;
        const std::uint32_t stored =
            static_cast<std::uint32_t>(data[pos]) |
            (static_cast<std::uint32_t>(data[pos + 1]) << 8);
        if (computed != stored) {
            return fail("The gzip header CRC16 does not match the header "
                        "bytes.");
        }
        pos += 2;
    }

    *deflateOffset = pos;
    return ParseResult::Ok;
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
            _stream.avail_in =
                static_cast<mz_uint32>(std::min(_inSize - _inPos, kChunk));
            _stream.next_out = out + *produced;
            _stream.avail_out =
                static_cast<mz_uint32>(std::min(outCap - *produced, kChunk));

            const mz_uint32 availInBefore = _stream.avail_in;
            const mz_uint32 availOutBefore = _stream.avail_out;
            const int status = mz_inflate(&_stream, MZ_SYNC_FLUSH);
            const std::size_t consumed = availInBefore - _stream.avail_in;
            const std::size_t got = availOutBefore - _stream.avail_out;

            if (got != 0) {
                _crc = static_cast<std::uint32_t>(
                    mz_crc32(_crc, out + *produced, got));
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

    bool Finished() const noexcept
    {
        return _finished;
    }
    bool InputExhausted() const noexcept
    {
        return _inPos == _inSize;
    }
    std::size_t ConsumedInput() const noexcept
    {
        return _inPos;
    }
    std::uint64_t TotalOutput() const noexcept
    {
        return _totalOut;
    }
    std::uint32_t Crc32() const noexcept
    {
        return _crc;
    }

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

// The field checks shared by both container generations.
bool ValidateHeaderFields(const SpzHeader* header, std::string* error)
{
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
    if (!IsGaussianCountWithinLimit(header->pointCount)) {
        SetError(error, diag::kImportLimitExceeded,
                 "The header declares " + std::to_string(header->pointCount) +
                     " Gaussians, above the shared import limit of " +
                     std::to_string(kMaxGaussianCount) + ".");
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

bool ValidateHeaderBytes(const unsigned char (&bytes)[kHeaderSize],
                         SpzHeader* header, std::string* error)
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

    if (header->version == kZstdContainerVersion) {
        // Version 4 is defined only as the plaintext zstd container; no
        // writer wraps a version-4 header in gzip.
        SetError(error, diag::kMalformedContainer,
                 "The gzip member carries SPZ version 4, which is defined only "
                 "as the plaintext ZSTD container; gzip-wrapped containers "
                 "are versions 1-3.");
        return false;
    }
    if (header->version < kMinSupportedVersion ||
        header->version > kMaxSupportedVersion) {
        if (header->version > kMaxSupportedVersion) {
            SetError(error, diag::kUnsupportedVersion,
                     "SPZ version " + std::to_string(header->version) +
                         " is not supported by this release; supported "
                         "container versions are 1-4.");
        } else {
            SetError(
                error, diag::kUnsupportedVersion,
                "SPZ version " + std::to_string(header->version) +
                    " is not a defined container version; supported versions "
                    "are 1-4.");
        }
        return false;
    }
    return ValidateHeaderFields(header, error);
}

std::uint64_t ExpectedPayloadBytes(const SpzHeader& header) noexcept
{
    // Attribute-major stream: positions, alphas, colors, scales, rotations,
    // spherical harmonics. pointCount <= INT32_MAX and the per-point width is
    // at most 92 bytes (v3, degree 4), so the product stays far below 2^63.
    const std::uint64_t perPoint =
        static_cast<std::uint64_t>(header.BytesPerPosition()) + 1 + // alpha
        3 +                                                         // color
        3 +                                                         // scale
        header.BytesPerRotation() +
        3 * static_cast<std::uint64_t>(header.ShDimensions());
    return perPoint * header.pointCount;
}

// The declared payload must be producible from the compressed bytes that are
// actually present. `compressedAvailable` excludes the gzip trailer.
bool CheckDeclaredSizePlausible(const SpzHeader& header,
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
        SetError(
            error, diag::kTruncatedContainer,
            "The header declares " + std::to_string(header.pointCount) +
                " Gaussians (" + std::to_string(expected) +
                " payload bytes), "
                "but the compressed stream is too small to contain them; the "
                "file is truncated.");
        return false;
    }
    return true;
}

// Decompresses exactly the 16 header bytes and validates them. Returns
// NeedMore when `complete` is false and the compressed prefix ran out before
// the header materialized.
ParseResult ReadHeaderFromBuffer(const std::vector<unsigned char>& data,
                                 bool complete, std::uint64_t fileSize,
                                 SpzHeader* header, std::string* error)
{
    std::size_t deflateOffset = 0;
    const ParseResult framing =
        ParseGzipHeader(data, complete, &deflateOffset, error);
    if (framing != ParseResult::Ok) {
        return framing;
    }

    RawInflator inflator;
    if (!inflator.Init(data.data() + deflateOffset,
                       data.size() - deflateOffset)) {
        SetError(error, diag::kInternalError,
                 "The DEFLATE decompressor could not be initialized.");
        return ParseResult::Malformed;
    }

    unsigned char headerBytes[kHeaderSize] = {};
    std::size_t produced = 0;
    const RawInflator::Status status =
        inflator.Pump(headerBytes, kHeaderSize, &produced);
    if (status == RawInflator::Status::Corrupt) {
        SetError(error, diag::kCorruptContainer,
                 "The DEFLATE stream is not decodable.");
        return ParseResult::Malformed;
    }
    if (produced < kHeaderSize) {
        if (status == RawInflator::Status::End) {
            SetError(error, diag::kMalformedContainer,
                     "The decompressed stream is shorter than the 16-byte SPZ "
                     "header.");
            return ParseResult::Malformed;
        }
        if (!complete) {
            return ParseResult::NeedMore;
        }
        SetError(error, diag::kTruncatedContainer,
                 "The compressed stream ends before the 16-byte SPZ header is "
                 "complete.");
        return ParseResult::Malformed;
    }

    if (!ValidateHeaderBytes(headerBytes, header, error)) {
        return ParseResult::Malformed;
    }

    // Trailer bytes are not decompressed input; require room for them.
    if (fileSize < deflateOffset + 8) {
        SetError(error, diag::kTruncatedContainer,
                 "The file is too small to hold the gzip trailer.");
        return ParseResult::Malformed;
    }
    if (!CheckDeclaredSizePlausible(*header, fileSize - deflateOffset - 8,
                                    error)) {
        return ParseResult::Malformed;
    }
    return ParseResult::Ok;
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
        ParseResult::Ok) {
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

// --------------------------------------------------------------------------
// Extension records (both generations)
// --------------------------------------------------------------------------

// Walks `[u32 type][u32 byteLength][payload]` records as the reference reader
// does: it stops at the first record that does not fit, and the bytes from
// there on are left unparsed rather than rejected.
void ParseExtensionRecords(SpzPackedDocument* document)
{
    const std::vector<unsigned char>& bytes = document->extensions;
    document->extensionRecords.clear();
    std::size_t pos = 0;
    while (bytes.size() - pos >= 8) {
        const std::uint32_t type = ReadLe32(bytes.data() + pos);
        const std::uint32_t length = ReadLe32(bytes.data() + pos + 4);
        if (length > bytes.size() - pos - 8) {
            break;
        }
        document->extensionRecords.push_back({type, pos + 8, length});
        pos += 8 + static_cast<std::size_t>(length);
    }
    document->unparsedExtensionBytes = bytes.size() - pos;
}

const char* CoordinateSystemName(std::uint32_t value) noexcept
{
    static const char* const kNames[] = {
        "UNSPECIFIED", "LDB", "RDB", "LUB", "RUB", "LDF", "RDF", "LUF", "RUF",
        "LFD",         "RFD", "LFU", "RFU", "LBD", "RBD", "LBU", "RBU"};
    return value <= kMaxCoordinateSystem ? kNames[value] : "unknown";
}

// SPZ_ADOBE_coordinate_system names the frame the stored data is in. The
// importer authors the data as the model's RUB frame without conversion
// (ADR 0001), so any other declared frame would be imported with the wrong
// orientation: it is refused as unsupported instead (SPZ_FORMAT.md §5).
bool CheckDeclaredCoordinateSystem(const SpzPackedDocument& document,
                                   std::string* error)
{
    for (const SpzExtensionRecord& record : document.extensionRecords) {
        if (record.type != kCoordinateSystemExtension) {
            continue;
        }
        if (record.size < 4) {
            SetError(error, diag::kMalformedContainer,
                     "The SPZ_ADOBE_coordinate_system extension record holds " +
                         std::to_string(record.size) +
                         " payload byte(s); it needs a 4-byte value.");
            return false;
        }
        const std::uint32_t value =
            ReadLe32(document.extensions.data() + record.offset);
        if (value == kCoordinateUnspecified || value == kCoordinateRub) {
            continue;
        }
        const std::string frame = value <= kMaxCoordinateSystem ?
                                      std::string(CoordinateSystemName(value)) :
                                      std::string("an undefined frame");
        SetError(error, diag::kUnsupportedCoordinateSystem,
                 "The SPZ_ADOBE_coordinate_system extension declares that the "
                 "data is stored in " +
                     frame + " (value " + std::to_string(value) +
                     "). This release imports only data stored in RUB, the "
                     "SPZ default; importing it unconverted would change its "
                     "orientation.");
        return false;
    }
    return true;
}

bool CheckExtensionBudget(std::uint64_t bytes, std::string* error)
{
    if (bytes > kMaxExtensionBytes) {
        SetError(error, diag::kImportLimitExceeded,
                 "SPZ extension records exceed the 64 MiB import limit.");
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// Version 4: plaintext header, extension zone, TOC, one zstd stream per
// attribute class (SPZ_FORMAT.md §4, ADR 0002 item 6)
// --------------------------------------------------------------------------

struct V4Stream {
    const char* name = "";
    std::size_t offset = 0;         // first compressed byte in the file
    std::size_t compressedSize = 0; // TOC compressedSize
    std::size_t payloadOffset = 0;  // destination in SpzPackedDocument
    std::size_t size = 0;           // TOC uncompressedSize == expected size
    std::size_t frameSize = 0;      // the one data frame at `offset`
};

struct V4Layout {
    SpzHeader header;
    std::size_t streamCount = 0;
    V4Stream streams[kMaxStreams];
};

// Validates everything that is stored in plaintext: the header, the extension
// zone and its declared frame, and the TOC against the header. `data` may be
// a prefix of the file; NeedMore asks for `*needed` bytes. Decompresses
// nothing and allocates nothing in proportion to the declared count.
ParseResult ParseV4Layout(const std::vector<unsigned char>& data,
                          std::uint64_t fileSize, V4Layout* layout,
                          SpzPackedDocument* extensionsOut, std::size_t* needed,
                          std::string* error)
{
    if (fileSize < kV4HeaderSize) {
        SetError(error, diag::kTruncatedContainer,
                 "The file ends before the 32-byte SPZ version 4 header is "
                 "complete.");
        return ParseResult::Malformed;
    }
    if (data.size() < kV4HeaderSize) {
        *needed = kV4HeaderSize;
        return ParseResult::NeedMore;
    }

    SpzHeader& header = layout->header;
    header = SpzHeader{};
    header.version = ReadLe32(data.data() + 4);
    header.pointCount = ReadLe32(data.data() + 8);
    header.shDegree = data[12];
    header.fractionalBits = data[13];
    header.flags = data[14];
    header.streamCount = data[15];
    header.tocByteOffset = ReadLe32(data.data() + 16);
    // Bytes 20-31 are reserved. The reference reader does not check them, and
    // neither does this one, so a file that loads there loads here.
    if (!ValidateHeaderFields(&header, error)) {
        return ParseResult::Malformed;
    }
    // The reference reader's count ceiling: refused before anything is sized
    // from the count.
    if (static_cast<std::uint64_t>(header.pointCount) * 9 >
        fileSize * kV4MaxCompressionRatio) {
        SetError(error, diag::kTruncatedContainer,
                 "The header declares " + std::to_string(header.pointCount) +
                     " Gaussians, but the " + std::to_string(fileSize) +
                     "-byte file is too small to contain them; the file is "
                     "truncated.");
        return ParseResult::Malformed;
    }

    // Extension zone [32, tocByteOffset).
    const std::uint64_t tocOffset = header.tocByteOffset;
    if (tocOffset < kV4HeaderSize) {
        SetError(error, diag::kMalformedContainer,
                 "The table of contents offset " + std::to_string(tocOffset) +
                     " points inside the 32-byte header.");
        return ParseResult::Malformed;
    }
    if (tocOffset > fileSize) {
        SetError(error, diag::kMalformedContainer,
                 "The table of contents offset " + std::to_string(tocOffset) +
                     " lies past the end of the " + std::to_string(fileSize) +
                     "-byte file.");
        return ParseResult::Malformed;
    }
    const std::uint64_t zoneSize = tocOffset - kV4HeaderSize;
    if (zoneSize != 0 && !header.HasExtensions()) {
        SetError(error, diag::kTrailingData,
                 std::to_string(zoneSize) +
                     " bytes lie between the header and the table of "
                     "contents, but the header does not declare extensions.");
        return ParseResult::Malformed;
    }
    if (!CheckExtensionBudget(zoneSize, error)) {
        return ParseResult::Malformed;
    }

    // Stream sizes implied by the header. pointCount <= INT32_MAX and every
    // per-point width is at most 72 bytes, so no product overflows 64 bits.
    const std::uint64_t count = header.pointCount;
    struct Expected {
        const char* name;
        std::uint64_t size;
    };
    const Expected expected[kMaxStreams] = {
        {"positions", count * header.BytesPerPosition()},
        {"alphas", count},
        {"colors", count * 3},
        {"scales", count * 3},
        {"rotations", count * header.BytesPerRotation()},
        {"spherical harmonics", count * 3 * header.ShDimensions()}};
    // Empty streams are omitted: SH degree 0 has five.
    const std::size_t expectedStreams = header.ShDimensions() == 0 ? 5 : 6;
    if (header.streamCount != expectedStreams) {
        SetError(error, diag::kMalformedContainer,
                 "The header declares " +
                     std::to_string(static_cast<int>(header.streamCount)) +
                     " streams; SH degree " +
                     std::to_string(static_cast<int>(header.shDegree)) +
                     " requires " + std::to_string(expectedStreams) + ".");
        return ParseResult::Malformed;
    }

    const std::uint64_t tocEnd =
        tocOffset +
        kTocEntryBytes * static_cast<std::uint64_t>(header.streamCount);
    if (tocEnd > fileSize) {
        SetError(error, diag::kTruncatedContainer,
                 "The file ends inside the table of contents.");
        return ParseResult::Malformed;
    }
    if (data.size() < tocEnd) {
        *needed = static_cast<std::size_t>(tocEnd);
        return ParseResult::NeedMore;
    }

    if (extensionsOut) {
        extensionsOut->extensions.assign(data.begin() + kV4HeaderSize,
                                         data.begin() + tocOffset);
        ParseExtensionRecords(extensionsOut);
        if (!CheckDeclaredCoordinateSystem(*extensionsOut, error)) {
            return ParseResult::Malformed;
        }
    }

    // The TOC: sizes must match the header, and the streams must exactly fill
    // the rest of the file.
    std::uint64_t cursor = tocEnd;
    std::uint64_t payloadCursor = 0;
    layout->streamCount = expectedStreams;
    for (std::size_t i = 0; i < expectedStreams; ++i) {
        const unsigned char* entry =
            data.data() + tocOffset + i * kTocEntryBytes;
        const std::uint64_t compressedSize = ReadLe64(entry);
        const std::uint64_t uncompressedSize = ReadLe64(entry + 8);
        if (uncompressedSize != expected[i].size) {
            SetError(error, diag::kMalformedContainer,
                     std::string("The ") + expected[i].name +
                         " stream declares " +
                         std::to_string(uncompressedSize) +
                         " uncompressed bytes; the header implies " +
                         std::to_string(expected[i].size) + ".");
            return ParseResult::Malformed;
        }
        if (compressedSize > fileSize - cursor) {
            SetError(
                error, diag::kTruncatedContainer,
                std::string("The file ends inside the ") + expected[i].name +
                    " stream: the table of contents declares " +
                    std::to_string(compressedSize) + " compressed bytes, " +
                    std::to_string(fileSize - cursor) + " remain.");
            return ParseResult::Malformed;
        }
        V4Stream& stream = layout->streams[i];
        stream.name = expected[i].name;
        stream.offset = static_cast<std::size_t>(cursor);
        stream.compressedSize = static_cast<std::size_t>(compressedSize);
        stream.payloadOffset = static_cast<std::size_t>(payloadCursor);
        stream.size = static_cast<std::size_t>(uncompressedSize);
        cursor += compressedSize;
        payloadCursor += uncompressedSize;
    }
    if (cursor != fileSize) {
        SetError(error, diag::kMalformedContainer,
                 "The table of contents accounts for " +
                     std::to_string(cursor) + " of the file's " +
                     std::to_string(fileSize) +
                     " bytes; the streams must fill the rest of the file.");
        return ParseResult::Malformed;
    }
    return ParseResult::Ok;
}

// Locates the one zstd data frame at the start of each stream and checks the
// frame layout before any allocation (ADR 0002 item 6, step 4).
bool CheckV4Frames(const std::vector<unsigned char>& file, V4Layout* layout,
                   std::uint64_t* skippableBytes, std::string* error)
{
    *skippableBytes = 0;
    for (std::size_t i = 0; i < layout->streamCount; ++i) {
        V4Stream& stream = layout->streams[i];
        const std::string where = std::string("the ") + stream.name + " stream";
        const unsigned char* src = file.data() + stream.offset;
        const bool last = i + 1 == layout->streamCount;

        if (stream.compressedSize >= 8 &&
            ZSTD_isSkippableFrame(src, stream.compressedSize)) {
            SetError(error, diag::kMalformedContainer,
                     "The first frame of " + where +
                         " is a skippable frame, not a data frame.");
            return false;
        }
        const std::size_t frameSize =
            ZSTD_findFrameCompressedSize(src, stream.compressedSize);
        if (ZSTD_isError(frameSize)) {
            if (ZSTD_getErrorCode(frameSize) == ZSTD_error_srcSize_wrong) {
                SetError(error, diag::kTruncatedContainer,
                         "The zstd frame of " + where +
                             " ends before its last block.");
            } else {
                SetError(error, diag::kCorruptContainer,
                         "The zstd frame of " + where + " is not decodable.");
            }
            return false;
        }
        const unsigned long long contentSize =
            ZSTD_getFrameContentSize(src, frameSize);
        if (contentSize == ZSTD_CONTENTSIZE_ERROR) {
            SetError(error, diag::kCorruptContainer,
                     "The zstd frame header of " + where +
                         " is not decodable.");
            return false;
        }
        if (contentSize != ZSTD_CONTENTSIZE_UNKNOWN &&
            contentSize != stream.size) {
            SetError(error, diag::kMalformedContainer,
                     "The zstd frame of " + where + " records " +
                         std::to_string(contentSize) +
                         " content bytes; the table of contents declares " +
                         std::to_string(stream.size) + ".");
            return false;
        }
        if (contentSize == ZSTD_CONTENTSIZE_UNKNOWN) {
            // Without a recorded size, the block count still bounds what the
            // frame can produce: refuse a frame that cannot fill the stream
            // before its buffer is allocated.
            const unsigned long long bound =
                ZSTD_decompressBound(src, frameSize);
            if (bound == ZSTD_CONTENTSIZE_ERROR || bound < stream.size) {
                SetError(error, diag::kTruncatedContainer,
                         "The zstd frame of " + where +
                             " is too short to hold the " +
                             std::to_string(stream.size) +
                             " bytes the table of contents declares.");
                return false;
            }
        }
        stream.frameSize = frameSize;

        if (!last) {
            if (frameSize != stream.compressedSize) {
                SetError(error, diag::kMalformedContainer,
                         std::to_string(stream.compressedSize - frameSize) +
                             " bytes follow the zstd frame of " + where +
                             "; only the last stream may carry more than one "
                             "frame.");
                return false;
            }
            continue;
        }
        // The last stream may end with skippable frames that carry extension
        // data. They are bounded and skipped, never decompressed.
        std::size_t pos = frameSize;
        while (pos < stream.compressedSize) {
            const std::size_t remaining = stream.compressedSize - pos;
            if (remaining < 8 || !ZSTD_isSkippableFrame(src + pos, remaining)) {
                SetError(error, diag::kMalformedContainer,
                         std::to_string(remaining) +
                             " bytes follow the zstd frame of " + where +
                             " and are not a skippable frame.");
                return false;
            }
            const std::size_t skipSize =
                ZSTD_findFrameCompressedSize(src + pos, remaining);
            if (ZSTD_isError(skipSize)) {
                SetError(error, diag::kTruncatedContainer,
                         "A skippable frame after " + where +
                             " ends past the end of the file.");
                return false;
            }
            pos += skipSize;
            *skippableBytes += skipSize;
        }
    }
    return true;
}

// Decompresses each data frame straight into its span of the payload: one
// shot, exact-size destination, one context reused for every stream.
bool DecodeV4Streams(const std::vector<unsigned char>& file,
                     const V4Layout& layout, std::vector<unsigned char>* out,
                     std::string* error)
{
    std::unique_ptr<ZSTD_DCtx, std::size_t (*)(ZSTD_DCtx*)> context(
        ZSTD_createDCtx(), &ZSTD_freeDCtx);
    if (!context) {
        SetError(error, diag::kInternalError,
                 "The ZSTD decompressor could not be initialized.");
        return false;
    }
    for (std::size_t i = 0; i < layout.streamCount; ++i) {
        const V4Stream& stream = layout.streams[i];
        const std::size_t produced = ZSTD_decompressDCtx(
            context.get(), out->data() + stream.payloadOffset, stream.size,
            file.data() + stream.offset, stream.frameSize);
        if (ZSTD_isError(produced)) {
            const ZSTD_ErrorCode code = ZSTD_getErrorCode(produced);
            const std::string reason =
                code == ZSTD_error_dstSize_tooSmall ?
                    "it decodes to more than the " +
                        std::to_string(stream.size) +
                        " bytes the table of contents declares" :
                code == ZSTD_error_checksum_wrong ?
                    std::string("its checksum does not match the decoded "
                                "data") :
                    "zstd error " + std::to_string(static_cast<int>(code));
            SetError(error, diag::kCorruptContainer,
                     std::string("The zstd frame of the ") + stream.name +
                         " stream is not decodable: " + reason + ".");
            return false;
        }
        if (produced != stream.size) {
            SetError(error, diag::kCorruptContainer,
                     std::string("The zstd frame of the ") + stream.name +
                         " stream decodes to " + std::to_string(produced) +
                         " bytes; the table of contents declares " +
                         std::to_string(stream.size) + ".");
            return false;
        }
    }
    return true;
}

// The whole v4 read after the file is in memory.
bool ReadV4(const std::vector<unsigned char>& file, SpzPackedDocument* document,
            std::string* error)
{
    V4Layout layout;
    std::size_t needed = 0;
    if (ParseV4Layout(file, file.size(), &layout, document, &needed, error) !=
        ParseResult::Ok) {
        // NeedMore cannot happen: `file` is the whole file.
        return false;
    }
    std::uint64_t skippableBytes = 0;
    if (!CheckV4Frames(file, &layout, &skippableBytes, error)) {
        return false;
    }

    const std::size_t expected =
        static_cast<std::size_t>(ExpectedPayloadBytes(layout.header));
    if (!TryResize(&document->payload, expected)) {
        SetError(error, diag::kModelAllocationFailed,
                 "The " + std::to_string(expected) +
                     "-byte SPZ payload could not be allocated.");
        return false;
    }
    if (!DecodeV4Streams(file, layout, &document->payload, error)) {
        return false;
    }

    document->header = layout.header;
    document->extensionFrameBytes = skippableBytes;
    const std::size_t count = layout.header.pointCount;
    const auto span = [&](std::size_t index, std::size_t size) {
        // SH degree 0 omits the last stream; its span is empty.
        return index < layout.streamCount ?
                   SpzPackedDocument::Span{layout.streams[index].payloadOffset,
                                           layout.streams[index].size} :
                   SpzPackedDocument::Span{expected, size};
    };
    document->positions = span(0, count * layout.header.BytesPerPosition());
    document->alphas = span(1, count);
    document->colors = span(2, count * 3);
    document->scales = span(3, count * 3);
    document->rotations = span(4, count * layout.header.BytesPerRotation());
    document->sh = span(5, 0);
    return true;
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
            // The v4 layout. Claimed whatever the version field says, so
            // Read() decodes v4 and explains any other version with its
            // specific diagnostic instead of USD reporting that no plugin
            // was found.
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
        // stored-block padding before the magic); retry once with the larger
        // bound. Still inconclusive past that means the file is declined
        // here rather than read in full during format resolution.
        if (!LoadFile(path, kCanReadRetryLimit, &data, &fileSize, nullptr)) {
            return false;
        }
        return CanReadBuffer(data, data.size() >= fileSize);
    } catch (...) {
        return false;
    }
}

bool SpzReader::ReadHeader(const std::string& path, SpzHeader* header,
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
    if (fileSize > kMaxSpzFileBytes) {
        SetError(error, diag::kImportLimitExceeded,
                 "The SPZ file is " + std::to_string(fileSize) +
                     " bytes, above the " + std::to_string(kMaxSpzFileBytes) +
                     "-byte input limit.");
        return false;
    }
    switch (DetectSignature(data)) {
    case Signature::PlaintextNgsp: {
        if (!CheckPlaintextVersion(data, error)) {
            return false;
        }
        V4Layout layout;
        SpzPackedDocument extensions;
        std::size_t needed = 0;
        ParseResult v4 =
            ParseV4Layout(data, fileSize, &layout, &extensions, &needed, error);
        if (v4 == ParseResult::NeedMore) {
            // The extension zone outgrew the prefix. ParseV4Layout has
            // already bounded it, so this read is bounded too.
            if (!LoadFile(path, needed, &data, &fileSize, error)) {
                return false;
            }
            v4 = ParseV4Layout(data, fileSize, &layout, &extensions, &needed,
                               error);
        }
        if (v4 != ParseResult::Ok) {
            if (v4 == ParseResult::NeedMore) {
                SetError(error, diag::kUnreadableFile,
                         "The file changed while it was being read: " + path);
            }
            return false;
        }
        *header = layout.header;
        return true;
    }
    case Signature::Unknown:
        SetError(error, diag::kNotSpzContainer,
                 "The file has neither the gzip signature of an SPZ v1-v3 "
                 "container nor a plaintext SPZ magic.");
        return false;
    case Signature::Gzip:
        break;
    }

    ParseResult result = ReadHeaderFromBuffer(data, data.size() >= fileSize,
                                              fileSize, header, error);
    if (result == ParseResult::NeedMore) {
        if (!LoadFile(path, static_cast<std::size_t>(kMaxSpzFileBytes), &data,
                      &fileSize, error)) {
            return false;
        }
        if (fileSize > kMaxSpzFileBytes) {
            SetError(error, diag::kImportLimitExceeded,
                     "The SPZ file grew beyond the " +
                         std::to_string(kMaxSpzFileBytes) +
                         "-byte input limit while it was being read.");
            return false;
        }
        result = ReadHeaderFromBuffer(data, true, fileSize, header, error);
    }
    return result == ParseResult::Ok;
}

bool SpzReader::Read(const std::string& path, SpzPackedDocument* document,
                     std::string* error) const
{
    if (!document) {
        SetError(error, diag::kInternalError,
                 "SpzReader received a null document output.");
        return false;
    }

    std::vector<unsigned char> file;
    std::uint64_t fileSize = 0;
    // Probe the size before the bulk read: loading up to the bound first and
    // checking afterwards would make an oversized file cost a full
    // kMaxSpzFileBytes allocation and read just to be refused.
    if (!LoadFile(path, 0, &file, &fileSize, error)) {
        return false;
    }
    if (fileSize > kMaxSpzFileBytes) {
        SetError(error, diag::kImportLimitExceeded,
                 "The SPZ file is " + std::to_string(fileSize) +
                     " bytes, above the " + std::to_string(kMaxSpzFileBytes) +
                     "-byte input limit.");
        return false;
    }
    if (!LoadFile(path, static_cast<std::size_t>(kMaxSpzFileBytes), &file,
                  &fileSize, error)) {
        return false;
    }
    // The file may grow between the two opens; only the bounded prefix was
    // read, so refuse rather than parse a truncated view of it.
    if (fileSize > kMaxSpzFileBytes) {
        SetError(error, diag::kImportLimitExceeded,
                 "The SPZ file grew beyond the " +
                     std::to_string(kMaxSpzFileBytes) +
                     "-byte input limit while it was being read.");
        return false;
    }
    document->extensions.clear();
    document->extensionRecords.clear();
    document->unparsedExtensionBytes = 0;
    document->extensionFrameBytes = 0;
    switch (DetectSignature(file)) {
    case Signature::PlaintextNgsp:
        return CheckPlaintextVersion(file, error) &&
               ReadV4(file, document, error);
    case Signature::Unknown:
        SetError(error, diag::kNotSpzContainer,
                 "The file has neither the gzip signature of an SPZ v1-v3 "
                 "container nor a plaintext SPZ magic.");
        return false;
    case Signature::Gzip:
        break;
    }

    std::size_t deflateOffset = 0;
    if (ParseGzipHeader(file, true, &deflateOffset, error) != ParseResult::Ok) {
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
    if (!CheckDeclaredSizePlausible(header, file.size() - deflateOffset - 8,
                                    error)) {
        return false;
    }

    // 2. The attribute-major payload, decompressed straight into its final
    // buffer. The plausibility bound above caps the allocation by what the
    // compressed bytes could actually produce.
    const std::size_t expected =
        static_cast<std::size_t>(ExpectedPayloadBytes(header));
    if (!TryResize(&document->payload, expected)) {
        SetError(error, diag::kModelAllocationFailed,
                 "The " + std::to_string(expected) +
                     "-byte SPZ payload could not be allocated.");
        return false;
    }
    std::fill(document->payload.begin(), document->payload.end(), 0);

    status = inflator.Pump(document->payload.data(), expected, &produced);
    if (status == RawInflator::Status::Corrupt) {
        SetError(error, diag::kCorruptContainer,
                 "The DEFLATE stream is not decodable.");
        return false;
    }
    if (produced < expected) {
        SetError(error, diag::kTruncatedContainer,
                 "The attribute streams end after " + std::to_string(produced) +
                     " of the " + std::to_string(expected) +
                     " bytes the header "
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
            if (!CheckExtensionBudget(static_cast<std::uint64_t>(produced) +
                                          document->extensions.size(),
                                      error)) {
                return false;
            }
            document->extensions.insert(document->extensions.end(), scratch,
                                        scratch + produced);
        } else {
            undeclaredTrailing += produced;
        }
        if (status == RawInflator::Status::Progress && produced == 0) {
            SetError(
                error, diag::kTruncatedContainer,
                "The compressed stream ends without a DEFLATE terminator.");
            return false;
        }
    }
    if (undeclaredTrailing != 0) {
        SetError(error, diag::kTrailingData,
                 std::to_string(undeclaredTrailing) +
                     " decompressed bytes follow "
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
        SetError(
            error, diag::kCorruptContainer,
            "The gzip length field does not match the decompressed stream.");
        return false;
    }
    if (file.size() > trailerOffset + 8) {
        SetError(error, diag::kTrailingData,
                 std::to_string(file.size() - trailerOffset - 8) +
                     " bytes follow "
                     "the gzip member.");
        return false;
    }

    // 5. Extension records, now that the member is known to be intact. A
    // declared frame other than RUB is refused here, as in v4.
    ParseExtensionRecords(document);
    if (!CheckDeclaredCoordinateSystem(*document, error)) {
        return false;
    }

    // 6. Fixed attribute-major slicing (SPZ_FORMAT.md §4).
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
