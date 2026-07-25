// SPDX-License-Identifier: Apache-2.0
#include "io/SogReader.h"

#include "io/GaussianSogDiagnostics.h"
#include "io/SogJson.h"
#include "openstrata/gs/GaussianSizeMath.h"

#include "miniz.h"
#include "webp/decode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openstrata::gs::sog {
namespace {

// The bundled layout's ZIP local-file signature at offset 0, and the entry
// every bundled SOG must carry (SOG_FORMAT.md §1).
constexpr unsigned char kZipSignature[4] = {'P', 'K', 0x03, 0x04};
constexpr const char* kMetaEntryName = "meta.json";

constexpr int kSupportedVersion = 2;
// Every SOG v2 codebook is 256 entries, quoted from write-sog.ts
// (SOG_MAPPING.md §2).
constexpr std::size_t kCodebookSize = 256;
// Palette centroids are laid out 64 per row (SOG_MAPPING.md §6).
constexpr std::size_t kShCentroidsPerRow = 64;
// Mirrors the SPZ container's INT32_MAX bound: every derived size then stays
// trivially inside 64 bits, and the per-plane `count <= width*height` check
// below is the tighter constraint in practice anyway.
constexpr std::size_t kMaxGaussianCount = 0x7fffffff;
// `meta.json` is a handful of small objects plus three 256-entry codebooks —
// tens of kilobytes. The cap exists so a hostile or misnamed file cannot make
// the parser allocate without bound; it is orders of magnitude above any real
// document.
constexpr std::size_t kMaxMetadataBytes = 4 * 1024 * 1024;
// Routing reads at most this much (SOG_FORMAT.md §6, the bounded prefix): a
// `.json` larger than this is declined rather than parsed during format
// resolution, because no SOG v2 `meta.json` approaches it.
constexpr std::size_t kCanReadPrefixLimit = 1024 * 1024;
// A decoded property plane is at most WebP's 16383x16383 at 4 bytes per texel;
// the compressed entry that produces it is far smaller. The cap bounds what a
// declared uncompressed size may make this reader allocate.
constexpr std::uint64_t kMaxPlaneBytes = 512ull * 1024ull * 1024ull;

// Bare messages become diagnostics here, so a code is chosen at every failure
// site rather than defaulted.
struct Failure {
    std::string* error;

    bool operator()(const char* code, const std::string& message) const
    {
        if (error) {
            *error = diag::Format(code, message);
        }
        return false;
    }
};

// Renders an integral-valued JSON number for a diagnostic message.
//
// `RequireIntegral` admits any double whose value is integral, which includes
// magnitudes far outside `long long` — and converting one of those to an
// integer type is undefined behaviour, not a wrapped or saturated result. The
// out-of-range diagnostics are exactly the ones handed such a value, so the
// conversion is guarded here rather than at each call site. 2^63 is exactly
// representable as a double, so the bound below is exact.
std::string FormatIntegral(double value)
{
    constexpr double kLongLongBound = 9223372036854775808.0; // 2^63
    if (value >= -kLongLongBound && value < kLongLongBound) {
        return std::to_string(static_cast<long long>(value));
    }
    // Beyond that range only the magnitude is meaningful to a reader, and the
    // classic locale keeps the spelling independent of the host's.
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << value;
    return text.str();
}

bool LoadFile(
    const std::string& path,
    std::size_t limit,
    std::vector<unsigned char>* out,
    std::uint64_t* fileSize,
    std::string* error)
{
    const Failure fail{error};
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return fail(diag::kUnreadableFile,
            "The file could not be opened for reading: " + path);
    }
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return fail(diag::kUnreadableFile,
            "The file size could not be determined: " + path);
    }
    *fileSize = static_cast<std::uint64_t>(size);
    const std::size_t want = static_cast<std::size_t>(
        std::min<std::uint64_t>(*fileSize, limit));
    if (!TryResize(out, want)) {
        return fail(diag::kUnreadableFile,
            "A " + std::to_string(want) + "-byte buffer for '" + path +
            "' could not be allocated.");
    }
    in.seekg(0);
    if (want != 0 &&
        !in.read(reinterpret_cast<char*>(out->data()),
                 static_cast<std::streamsize>(want))) {
        return fail(diag::kUnreadableFile,
            "The file could not be read: " + path);
    }
    return true;
}

bool HasZipSignature(const std::vector<unsigned char>& data) noexcept
{
    return data.size() >= sizeof kZipSignature &&
        std::equal(kZipSignature, kZipSignature + sizeof kZipSignature,
                   data.data());
}

// Distinguishes "this is not a SOG container at all" from "this is a SOG
// meta.json with something wrong in it". Without the split, a JPEG handed to
// this reader would be reported as malformed JSON, which tells a user nothing.
// A UTF-8 BOM is tolerated here; the parser itself rejects it, and a document
// that gets that far is a real (if defective) JSON attempt.
bool LooksLikeJsonObject(const std::vector<unsigned char>& data) noexcept
{
    std::size_t position = 0;
    if (data.size() >= 3 && data[0] == 0xef && data[1] == 0xbb &&
        data[2] == 0xbf) {
        position = 3;
    }
    while (position < data.size()) {
        const unsigned char byte = data[position];
        if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r') {
            ++position;
            continue;
        }
        return byte == '{';
    }
    return false;
}

bool RejectOversizedMetadata(std::uint64_t size, std::string* error)
{
    return Failure{error}(diag::kMalformedMetadata,
        "meta.json is " + std::to_string(size) + " bytes, above the " +
        std::to_string(kMaxMetadataBytes) +
        "-byte bound for a SOG v2 document.");
}

// Reads a container whole, under the bound its own layout admits.
//
// The layout is decided from the 4-byte ZIP signature before the bulk read,
// because the two bounds differ by orders of magnitude: a bundled archive is
// as large as its planes require, while a `meta.json` above kMaxMetadataBytes
// is refused outright. Probing first is what keeps a mis-named or hostile
// multi-gigabyte `.json` from being loaded in full only to be rejected by the
// parser afterwards. ParseMetaJson keeps its own check as the backstop: the
// file may grow between the two opens.
bool LoadContainer(
    const std::string& path,
    std::vector<unsigned char>* data,
    std::uint64_t* fileSize,
    bool* bundled,
    std::string* error)
{
    std::vector<unsigned char> prefix;
    std::uint64_t probedSize = 0;
    if (!LoadFile(path, sizeof kZipSignature, &prefix, &probedSize, error)) {
        return false;
    }
    *bundled = HasZipSignature(prefix);
    if (!*bundled && probedSize > kMaxMetadataBytes) {
        return RejectOversizedMetadata(probedSize, error);
    }
    return LoadFile(path, std::numeric_limits<std::size_t>::max(), data,
                    fileSize, error);
}

std::string DirectoryOf(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// The reference decoder for the unbundled layout: the plane sits beside
// `meta.json`. Plane names are validated before they reach here, so this
// never composes a path that escapes that directory.
bool LoadCompanionFromDirectory(
    const std::string& anchorPath,
    const std::string& planeName,
    std::vector<unsigned char>* bytes,
    std::string* error)
{
    std::uint64_t size = 0;
    const std::string path = DirectoryOf(anchorPath) + planeName;
    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
        return Failure{error}(diag::kMissingPlane,
            "The property plane '" + planeName + "' was not found beside "
            "meta.json (expected at '" + path + "').");
    }
    probe.close();
    return LoadFile(path, std::numeric_limits<std::size_t>::max(), bytes,
                    &size, error);
}

// --- meta.json ---------------------------------------------------------------

// Windows resolves these names to character devices in *every* directory and
// with any extension, so an unbundled plane named "COM1" or "NUL.webp" beside
// `meta.json` opens a device rather than a file — a serial port open can even
// block. Rejected on every platform, so a given `meta.json` is accepted or
// refused identically everywhere rather than only on the host that is exposed.
bool IsReservedDeviceName(const std::string& name) noexcept
{
    static constexpr const char* kReserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

    // Only the stem is reserved, and Windows strips trailing spaces and dots
    // before resolving, so "NUL.webp" and "nul " are the device too. Compared
    // through a view rather than a built-up string: the caller is noexcept.
    std::string_view stem(name);
    stem = stem.substr(0, stem.find('.'));
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) {
        stem.remove_suffix(1);
    }
    for (const char* reserved : kReserved) {
        const std::string_view candidate(reserved);
        if (candidate.size() != stem.size()) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < stem.size() && same; ++i) {
            char c = stem[i];
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
            same = c == candidate[i];
        }
        if (same) {
            return true;
        }
    }
    return false;
}

// A `files` entry names one plane in the archive, or one companion beside
// `meta.json`. Both are looked up verbatim, so the name is confined to a bare
// file name: a hostile `meta.json` must not be able to reach outside its own
// directory through '..' or an absolute path, nor name something that is not a
// file at all.
bool IsSafePlaneName(const std::string& name) noexcept
{
    if (name.empty() || name.size() > 255 || name == "." || name == "..") {
        return false;
    }
    for (const char c : name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    return !IsReservedDeviceName(name);
}

// Every `meta.json` fact this reader needs, still in container terms.
struct MetaDocument {
    SogMetadata metadata;
    float meansMinimum[3] = {0.0f, 0.0f, 0.0f};
    float meansMaximum[3] = {0.0f, 0.0f, 0.0f};
    std::vector<float> scalesCodebook;
    std::vector<float> sh0Codebook;
    std::vector<float> shCodebook;
    std::size_t shPaletteCount = 0;

    // Plane file names, in the order SOG_MAPPING.md §2 documents them: the
    // low then high position bytes, and the SH centroids then labels. The
    // order is positional in `files`, as the reference writer emits it.
    std::string meansLowFile;
    std::string meansHighFile;
    std::string scalesFile;
    std::string quatsFile;
    std::string sh0File;
    std::string shCentroidsFile;
    std::string shLabelsFile;
};

const JsonValue* RequireObject(
    const JsonValue& parent,
    const char* key,
    std::string* error)
{
    const JsonValue* value = parent.Find(key);
    if (!value) {
        Failure{error}(diag::kMalformedMetadata,
            std::string("meta.json has no \"") + key + "\" property.");
        return nullptr;
    }
    if (!value->IsObject()) {
        Failure{error}(diag::kMalformedMetadata,
            std::string("meta.json \"") + key + "\" is not an object.");
        return nullptr;
    }
    return value;
}

// JSON has one numeric type, so "integer" is a value constraint, not a type:
// `2.0` is a valid version and `2.5` is not.
bool RequireIntegral(
    const JsonValue& parent,
    const char* key,
    const char* code,
    double* out,
    std::string* error)
{
    const Failure fail{error};
    const JsonValue* value = parent.Find(key);
    if (!value) {
        return fail(diag::kMalformedMetadata,
            std::string("meta.json has no \"") + key + "\" field.");
    }
    if (!value->IsNumber()) {
        return fail(diag::kMalformedMetadata,
            std::string("meta.json \"") + key + "\" is not a number.");
    }
    if (value->number != std::floor(value->number)) {
        return fail(code,
            std::string("meta.json \"") + key + "\" is " +
            std::to_string(value->number) + ", which is not an integer.");
    }
    *out = value->number;
    return true;
}

bool RequireFloatArray(
    const JsonValue& parent,
    const char* key,
    std::size_t expected,
    const char* code,
    std::vector<float>* out,
    std::string* error)
{
    const Failure fail{error};
    const JsonValue* value = parent.Find(key);
    if (!value) {
        return fail(code,
            std::string("meta.json has no \"") + key + "\" array.");
    }
    if (!value->IsArray() || value->items.size() != expected) {
        return fail(code,
            std::string("meta.json \"") + key + "\" must be an array of " +
            std::to_string(expected) + " numbers.");
    }
    if (!TryResize(out, expected)) {
        return fail(diag::kModelAllocationFailed,
            std::string("The \"") + key + "\" array could not be allocated.");
    }
    for (std::size_t i = 0; i < expected; ++i) {
        const JsonValue& item = value->items[i];
        if (!item.IsNumber()) {
            return fail(code,
                std::string("meta.json \"") + key + "\"[" +
                std::to_string(i) + "] is not a number.");
        }
        // The parser rejects non-finite JSON numbers, so only a magnitude
        // beyond float range can arrive here.
        const float single = static_cast<float>(item.number);
        if (!std::isfinite(single)) {
            return fail(code,
                std::string("meta.json \"") + key + "\"[" +
                std::to_string(i) + "] is outside the range of a 32-bit "
                "float.");
        }
        (*out)[i] = single;
    }
    return true;
}

// `files` is the plane list of one property: exactly `expected` bare file
// names, in the documented order.
bool RequireFiles(
    const JsonValue& property,
    const char* propertyName,
    std::size_t expected,
    std::vector<std::string>* names,
    std::string* error)
{
    const Failure fail{error};
    const JsonValue* files = property.Find("files");
    if (!files || !files->IsArray()) {
        return fail(diag::kMalformedMetadata,
            std::string("meta.json \"") + propertyName +
            "\" has no \"files\" array.");
    }
    if (files->items.size() != expected) {
        return fail(diag::kMalformedMetadata,
            std::string("meta.json \"") + propertyName + "\".files lists " +
            std::to_string(files->items.size()) + " file(s); SOG v2 declares " +
            std::to_string(expected) + ".");
    }
    names->clear();
    for (const JsonValue& item : files->items) {
        if (!item.IsString() || !IsSafePlaneName(item.text)) {
            return fail(diag::kMalformedMetadata,
                std::string("meta.json \"") + propertyName +
                "\".files must list plain file names: no directory component, "
                "and no name reserved for a character device.");
        }
        names->push_back(item.text);
    }
    return true;
}

bool ParseMetaJson(
    const std::vector<unsigned char>& bytes,
    MetaDocument* meta,
    std::string* error)
{
    const Failure fail{error};
    if (bytes.size() > kMaxMetadataBytes) {
        return RejectOversizedMetadata(bytes.size(), error);
    }

    JsonValue root;
    std::string parseError;
    if (!ParseJson(reinterpret_cast<const char*>(bytes.data()), bytes.size(),
                   &root, &parseError)) {
        return fail(diag::kMalformedMetadata,
            "meta.json is not valid JSON: " + parseError + ".");
    }
    if (!root.IsObject()) {
        return fail(diag::kMalformedMetadata,
            "meta.json is not a JSON object.");
    }

    // Version first, so a legacy file is told its version is unsupported
    // rather than that its (differently shaped) contents are malformed.
    const JsonValue* version = root.Find("version");
    if (!version) {
        return fail(diag::kUnsupportedVersion,
            "meta.json has no \"version\" field, which identifies legacy "
            "SOG v1. This release reads SOG v2; re-export the asset with a "
            "current PlayCanvas SplatTransform or SuperSplat.");
    }
    double versionNumber = 0.0;
    if (!RequireIntegral(root, "version", diag::kUnsupportedVersion,
                         &versionNumber, error)) {
        return false;
    }
    if (versionNumber != static_cast<double>(kSupportedVersion)) {
        return fail(diag::kUnsupportedVersion,
            "SOG version " + FormatIntegral(versionNumber) +
            " is not supported by this release; supported version is " +
            std::to_string(kSupportedVersion) + ".");
    }
    meta->metadata.version = kSupportedVersion;

    double count = 0.0;
    if (!RequireIntegral(root, "count", diag::kInvalidGaussianCount, &count,
                         error)) {
        return false;
    }
    if (count < 0.0 || count > static_cast<double>(kMaxGaussianCount)) {
        return fail(diag::kInvalidGaussianCount,
            "meta.json declares " + FormatIntegral(count) +
            " Gaussians, outside the supported range 0-" +
            std::to_string(kMaxGaussianCount) + ".");
    }
    if (count == 0.0) {
        // Valid SOG, but the shared model requires at least one Gaussian and
        // this plugin never authors a stage that misrepresents its source
        // (GAUSSIAN_MODEL_CONTRACT.md §3).
        return fail(diag::kEmptyPointSet,
            "meta.json declares zero Gaussians; there is nothing to import.");
    }
    meta->metadata.gaussianCount = static_cast<std::size_t>(count);

    const JsonValue* means = RequireObject(root, "means", error);
    if (!means) {
        return false;
    }
    std::vector<float> range;
    if (!RequireFloatArray(*means, "mins", 3, diag::kMalformedMetadata, &range,
                           error)) {
        return false;
    }
    std::copy(range.begin(), range.end(), meta->meansMinimum);
    if (!RequireFloatArray(*means, "maxs", 3, diag::kMalformedMetadata, &range,
                           error)) {
        return false;
    }
    std::copy(range.begin(), range.end(), meta->meansMaximum);
    std::vector<std::string> files;
    if (!RequireFiles(*means, "means", 2, &files, error)) {
        return false;
    }
    meta->meansLowFile = files[0];
    meta->meansHighFile = files[1];

    const JsonValue* scales = RequireObject(root, "scales", error);
    if (!scales) {
        return false;
    }
    if (!RequireFloatArray(*scales, "codebook", kCodebookSize,
                           diag::kInvalidCodebook, &meta->scalesCodebook,
                           error) ||
        !RequireFiles(*scales, "scales", 1, &files, error)) {
        return false;
    }
    meta->scalesFile = files[0];

    const JsonValue* quats = RequireObject(root, "quats", error);
    if (!quats || !RequireFiles(*quats, "quats", 1, &files, error)) {
        return false;
    }
    meta->quatsFile = files[0];

    const JsonValue* sh0 = RequireObject(root, "sh0", error);
    if (!sh0) {
        return false;
    }
    if (!RequireFloatArray(*sh0, "codebook", kCodebookSize,
                           diag::kInvalidCodebook, &meta->sh0Codebook,
                           error) ||
        !RequireFiles(*sh0, "sh0", 1, &files, error)) {
        return false;
    }
    meta->sh0File = files[0];

    // `shN` is optional: a SOG without it is a degree-0 cloud.
    const JsonValue* shN = root.Find("shN");
    if (!shN || shN->IsNull()) {
        return true;
    }
    if (!shN->IsObject()) {
        return fail(diag::kMalformedMetadata,
            "meta.json \"shN\" is present but is not an object.");
    }
    double bands = 0.0;
    if (!RequireIntegral(*shN, "bands", diag::kInvalidShBands, &bands, error)) {
        return false;
    }
    if (bands < 1.0 || bands > 3.0) {
        return fail(diag::kInvalidShBands,
            "meta.json declares " + FormatIntegral(bands) +
            " spherical-harmonic band(s); SOG v2 admits 1-3.");
    }
    meta->metadata.shBands = static_cast<int>(bands);

    double paletteCount = 0.0;
    if (!RequireIntegral(*shN, "count", diag::kMalformedMetadata,
                         &paletteCount, error)) {
        return false;
    }
    if (paletteCount < 0.0 ||
        paletteCount > static_cast<double>(kMaxGaussianCount)) {
        return fail(diag::kMalformedMetadata,
            "meta.json \"shN\".count is " + FormatIntegral(paletteCount) +
            ", outside the supported range.");
    }
    meta->shPaletteCount = static_cast<std::size_t>(paletteCount);

    if (!RequireFloatArray(*shN, "codebook", kCodebookSize,
                           diag::kInvalidCodebook, &meta->shCodebook, error) ||
        !RequireFiles(*shN, "shN", 2, &files, error)) {
        return false;
    }
    meta->shCentroidsFile = files[0];
    meta->shLabelsFile = files[1];
    return true;
}

// --- WebP planes -------------------------------------------------------------

// Lossless (VP8L) RGBA decode through the vendored libwebp decoder subset.
// A lossy plane is rejected rather than decoded approximately: SOG property
// images are lossless by specification and lossy positions would be silently
// wrong (SOG_FORMAT.md §3).
bool DecodePlane(
    const std::vector<unsigned char>& bytes,
    const std::string& name,
    SogPlane* plane,
    std::string* error)
{
    const Failure fail{error};
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(bytes.data(), bytes.size(), &features) !=
        VP8_STATUS_OK) {
        return fail(diag::kMalformedPlane,
            "The property plane '" + name + "' is not a decodable WebP "
            "image.");
    }
    // WebPBitstreamFeatures::format is 1 for lossy, 2 for lossless, 0 for an
    // undefined or mixed source.
    if (features.format != 2) {
        return fail(diag::kMalformedPlane,
            "The property plane '" + name + "' is not lossless WebP; SOG "
            "property images are lossless by specification, and a lossy "
            "plane would silently corrupt the values it carries.");
    }
    if (features.width <= 0 || features.height <= 0) {
        return fail(diag::kMalformedPlane,
            "The property plane '" + name + "' declares an empty image.");
    }

    std::size_t texels = 0;
    std::size_t byteCount = 0;
    if (!CheckedMulSize(static_cast<std::size_t>(features.width),
                        static_cast<std::size_t>(features.height), &texels) ||
        !CheckedMulSize(texels, 4, &byteCount)) {
        return fail(diag::kMalformedPlane,
            "The property plane '" + name + "' declares dimensions whose "
            "decoded size is not addressable on this platform.");
    }
    if (!TryResize(&plane->rgba, byteCount)) {
        return fail(diag::kModelAllocationFailed,
            "A " + std::to_string(byteCount) + "-byte buffer for the "
            "property plane '" + name + "' could not be allocated.");
    }
    if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), plane->rgba.data(),
                            plane->rgba.size(),
                            features.width * 4)) {
        plane->rgba.clear();
        return fail(diag::kMalformedPlane,
            "The property plane '" + name + "' could not be decoded; its "
            "WebP bitstream is corrupt.");
    }
    plane->width = static_cast<std::uint32_t>(features.width);
    plane->height = static_cast<std::uint32_t>(features.height);
    return true;
}

// Every per-Gaussian plane is indexed `i = x + y*width` with its own width, so
// planes need not agree on dimensions — each one only has to hold `count`
// texels (SOG_MAPPING.md §2).
bool CheckPerGaussianPlane(
    const SogPlane& plane,
    std::size_t count,
    const std::string& name,
    std::string* error)
{
    std::size_t texels = 0;
    if (!CheckedMulSize(plane.width, plane.height, &texels)) {
        return Failure{error}(diag::kMalformedPlane,
            "The property plane '" + name + "' declares an unusable size.");
    }
    if (count > texels) {
        return Failure{error}(diag::kInvalidGaussianCount,
            "meta.json declares " + std::to_string(count) +
            " Gaussians, but the property plane '" + name + "' holds only " +
            std::to_string(texels) + " texel(s) (" +
            std::to_string(plane.width) + "x" + std::to_string(plane.height) +
            ").");
    }
    return true;
}

// --- ZIP (bundled layout) ----------------------------------------------------

// The bundled layout's archive, read from memory through the vendored miniz
// with its archive-reading API enabled for this bundle only. The central
// directory is walked once so entry lookup is by name, as the format's own
// `files` lists address it.
class ZipArchive {
public:
    ~ZipArchive()
    {
        if (_initialized) {
            mz_zip_reader_end(&_zip);
        }
    }

    bool Init(
        const std::vector<unsigned char>& data,
        std::string* error)
    {
        const Failure fail{error};
        _zip = mz_zip_archive{};
        if (!mz_zip_reader_init_mem(&_zip, data.data(), data.size(), 0)) {
            return fail(diag::kMalformedArchive,
                "The .sog archive's ZIP central directory could not be read.");
        }
        _initialized = true;

        const mz_uint entries = mz_zip_reader_get_num_files(&_zip);
        for (mz_uint index = 0; index < entries; ++index) {
            if (mz_zip_reader_is_file_a_directory(&_zip, index)) {
                continue;
            }
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&_zip, index, &stat)) {
                return fail(diag::kMalformedArchive,
                    "The .sog archive entry at index " +
                    std::to_string(index) + " has an unreadable header.");
            }
            _entries.emplace_back(stat.m_filename, index);
        }
        return true;
    }

    bool Has(const std::string& name) const noexcept
    {
        return Find(name) != nullptr;
    }

    // Extracts one entry whole. `missingCode` lets the caller distinguish a
    // missing property plane from a ZIP that is not a SOG bundle at all.
    bool Extract(
        const std::string& name,
        const char* missingCode,
        const std::string& missingMessage,
        std::vector<unsigned char>* out,
        std::string* error) const
    {
        const Failure fail{error};
        const mz_uint* index = Find(name);
        if (!index) {
            return fail(missingCode, missingMessage);
        }
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&_zip, *index, &stat)) {
            return fail(diag::kMalformedArchive,
                "The .sog archive entry '" + name + "' has an unreadable "
                "header.");
        }
        if (stat.m_uncomp_size > kMaxPlaneBytes) {
            return fail(diag::kMalformedArchive,
                "The .sog archive entry '" + name + "' declares " +
                std::to_string(stat.m_uncomp_size) + " uncompressed bytes, "
                "above the " + std::to_string(kMaxPlaneBytes) + "-byte bound "
                "this reader will allocate for one entry.");
        }
        if (!TryResize(out, static_cast<std::size_t>(stat.m_uncomp_size))) {
            return fail(diag::kModelAllocationFailed,
                "A " + std::to_string(stat.m_uncomp_size) + "-byte buffer "
                "for the .sog archive entry '" + name + "' could not be "
                "allocated.");
        }
        if (!out->empty() &&
            !mz_zip_reader_extract_to_mem(
                &_zip, *index, out->data(), out->size(), 0)) {
            return fail(diag::kMalformedArchive,
                "The .sog archive entry '" + name + "' could not be "
                "decompressed; its data is corrupt.");
        }
        return true;
    }

private:
    const mz_uint* Find(const std::string& name) const noexcept
    {
        for (const auto& entry : _entries) {
            if (entry.first == name) {
                return &entry.second;
            }
        }
        return nullptr;
    }

    // miniz's reader API takes a non-const archive even to stat an entry, so
    // the handle is mutable rather than cast away at each call: extraction
    // does not change the archive as this class exposes it.
    mutable mz_zip_archive _zip{};
    bool _initialized = false;
    std::vector<std::pair<std::string, mz_uint>> _entries;
};

} // namespace

SogReader::SogReader(SogCompanionLoader companionLoader)
    : _companionLoader(std::move(companionLoader))
{
}

bool SogReader::CanReadBundled(const std::string& path) const noexcept
{
    try {
        std::vector<unsigned char> prefix;
        std::uint64_t fileSize = 0;
        if (!LoadFile(path, sizeof kZipSignature, &prefix, &fileSize,
                      nullptr)) {
            return false;
        }
        // The signature alone, deliberately: whether the archive holds a
        // usable meta.json is Read()'s question, so a damaged bundle gets a
        // specific diagnostic instead of being disowned (SOG_FORMAT.md §6).
        return HasZipSignature(prefix);
    } catch (...) {
        return false;
    }
}

bool SogReader::CanReadUnbundled(const std::string& path) const noexcept
{
    try {
        std::vector<unsigned char> data;
        std::uint64_t fileSize = 0;
        if (!LoadFile(path, kCanReadPrefixLimit, &data, &fileSize, nullptr)) {
            return false;
        }
        if (fileSize > data.size()) {
            return false; // larger than any SOG v2 meta.json
        }

        JsonValue root;
        if (!ParseJson(reinterpret_cast<const char*>(data.data()), data.size(),
                       &root, nullptr) ||
            !root.IsObject()) {
            return false;
        }
        // The strict gate that keeps a `.json` registration from claiming
        // unrelated JSON: the supported version *and* all four required SOG
        // properties, each with its plane list.
        const JsonValue* version = root.Find("version");
        if (!version || !version->IsNumber() ||
            version->number != static_cast<double>(kSupportedVersion)) {
            return false;
        }
        for (const char* property : {"means", "scales", "quats", "sh0"}) {
            const JsonValue* value = root.Find(property);
            if (!value || !value->IsObject()) {
                return false;
            }
            const JsonValue* files = value->Find("files");
            if (!files || !files->IsArray() || files->items.empty()) {
                return false;
            }
            for (const JsonValue& item : files->items) {
                if (!item.IsString()) {
                    return false;
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool SogReader::ReadMetadata(
    const std::string& path,
    SogMetadata* metadata,
    std::string* error) const
{
    const Failure fail{error};
    if (!metadata) {
        return fail(diag::kInternalError,
            "SogReader received a null metadata output.");
    }

    std::vector<unsigned char> data;
    std::uint64_t fileSize = 0;
    bool bundled = false;
    if (!LoadContainer(path, &data, &fileSize, &bundled, error)) {
        return false;
    }

    MetaDocument meta;
    if (bundled) {
        ZipArchive archive;
        if (!archive.Init(data, error)) {
            return false;
        }
        std::vector<unsigned char> metaBytes;
        if (!archive.Extract(kMetaEntryName, diag::kNotSogContainer,
                "The .sog archive contains no 'meta.json' entry, so it is not "
                "a bundled SOG container.", &metaBytes, error)) {
            return false;
        }
        if (!ParseMetaJson(metaBytes, &meta, error)) {
            return false;
        }
    } else {
        if (!LooksLikeJsonObject(data)) {
            return fail(diag::kNotSogContainer,
                "The file is neither a bundled SOG archive (a ZIP holding "
                "meta.json) nor a SOG meta.json document.");
        }
        if (!ParseMetaJson(data, &meta, error)) {
            return false;
        }
    }

    *metadata = meta.metadata;
    return true;
}

bool SogReader::Read(
    const std::string& path,
    SogDocument* document,
    std::string* error) const
{
    const Failure fail{error};
    if (!document) {
        return fail(diag::kInternalError,
            "SogReader received a null document output.");
    }

    // Layout detection by content (SOG_FORMAT.md §1): a ZIP signature is the
    // bundled layout, anything else is read as an unbundled meta.json.
    std::vector<unsigned char> data;
    std::uint64_t fileSize = 0;
    bool bundled = false;
    if (!LoadContainer(path, &data, &fileSize, &bundled, error)) {
        return false;
    }

    ZipArchive archive;
    std::vector<unsigned char> metaBytes;
    if (bundled) {
        if (!archive.Init(data, error)) {
            return false;
        }
        if (!archive.Extract(kMetaEntryName, diag::kNotSogContainer,
                "The .sog archive contains no 'meta.json' entry, so it is not "
                "a bundled SOG container.", &metaBytes, error)) {
            return false;
        }
    } else {
        if (!LooksLikeJsonObject(data)) {
            return fail(diag::kNotSogContainer,
                "The file is neither a bundled SOG archive (a ZIP holding "
                "meta.json) nor a SOG meta.json document.");
        }
        metaBytes = data;
    }

    MetaDocument meta;
    if (!ParseMetaJson(metaBytes, &meta, error)) {
        return false;
    }

    SogDocument result;
    result.metadata = meta.metadata;
    std::copy(meta.meansMinimum, meta.meansMinimum + 3, result.meansMinimum);
    std::copy(meta.meansMaximum, meta.meansMaximum + 3, result.meansMaximum);
    result.scalesCodebook = std::move(meta.scalesCodebook);
    result.sh0Codebook = std::move(meta.sh0Codebook);
    result.shCodebook = std::move(meta.shCodebook);
    result.shPaletteCount = meta.shPaletteCount;
    result.sourceBytes = bundled ? fileSize : static_cast<std::uint64_t>(
        metaBytes.size());

    const SogCompanionLoader& companionLoader = _companionLoader;
    const auto loadPlane = [&](const std::string& name, SogPlane* plane) {
        std::vector<unsigned char> bytes;
        if (bundled) {
            if (!archive.Extract(name, diag::kMissingPlane,
                    "The property plane '" + name + "' declared by meta.json "
                    "is not in the .sog archive.", &bytes, error)) {
                return false;
            }
        } else {
            const bool loaded = companionLoader
                ? companionLoader(path, name, &bytes, error)
                : LoadCompanionFromDirectory(path, name, &bytes, error);
            if (!loaded) {
                return false;
            }
            result.sourceBytes += bytes.size();
        }
        return DecodePlane(bytes, name, plane, error);
    };

    if (!loadPlane(meta.meansLowFile, &result.meansLow) ||
        !loadPlane(meta.meansHighFile, &result.meansHigh) ||
        !loadPlane(meta.scalesFile, &result.scales) ||
        !loadPlane(meta.quatsFile, &result.quats) ||
        !loadPlane(meta.sh0File, &result.sh0)) {
        return false;
    }

    const std::size_t count = result.metadata.gaussianCount;
    if (!CheckPerGaussianPlane(result.meansLow, count, meta.meansLowFile,
                               error) ||
        !CheckPerGaussianPlane(result.meansHigh, count, meta.meansHighFile,
                               error) ||
        !CheckPerGaussianPlane(result.scales, count, meta.scalesFile, error) ||
        !CheckPerGaussianPlane(result.quats, count, meta.quatsFile, error) ||
        !CheckPerGaussianPlane(result.sh0, count, meta.sh0File, error)) {
        return false;
    }

    if (result.metadata.shBands != 0) {
        if (!loadPlane(meta.shCentroidsFile, &result.shCentroids) ||
            !loadPlane(meta.shLabelsFile, &result.shLabels)) {
            return false;
        }
        if (!CheckPerGaussianPlane(result.shLabels, count, meta.shLabelsFile,
                                   error)) {
            return false;
        }
        // Centroids are addressed `x = (label % 64) * shCoeffs + coeff`,
        // `y = label / 64` (SOG_MAPPING.md §6), so the plane must be exactly
        // one palette wide and hold every declared centroid row.
        const int bands = result.metadata.shBands;
        const std::size_t coefficients =
            static_cast<std::size_t>(bands) * (static_cast<std::size_t>(bands) + 2);
        const std::size_t expectedWidth = kShCentroidsPerRow * coefficients;
        if (result.shCentroids.width != expectedWidth) {
            return fail(diag::kMalformedPlane,
                "The spherical-harmonic centroid plane '" +
                meta.shCentroidsFile + "' is " +
                std::to_string(result.shCentroids.width) + " texels wide; " +
                std::to_string(bands) + " band(s) require " +
                std::to_string(expectedWidth) + ".");
        }
        std::size_t centroidCapacity = 0;
        if (!CheckedMulSize(kShCentroidsPerRow, result.shCentroids.height,
                            &centroidCapacity) ||
            result.shPaletteCount > centroidCapacity) {
            return fail(diag::kMalformedPlane,
                "meta.json declares " + std::to_string(result.shPaletteCount) +
                " spherical-harmonic palette centroid(s), but '" +
                meta.shCentroidsFile + "' holds only " +
                std::to_string(centroidCapacity) + ".");
        }
    }

    *document = std::move(result);
    return true;
}

} // namespace openstrata::gs::sog
