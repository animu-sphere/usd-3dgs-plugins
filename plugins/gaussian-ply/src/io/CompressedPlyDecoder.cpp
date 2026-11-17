// SPDX-License-Identifier: Apache-2.0
#include "io/CompressedPlyDecoder.h"

#include "io/GaussianPlyDiagnostics.h"
#include "openstrata/gs/GaussianImportLimits.h"
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/GaussianSizeMath.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace openstrata::gs::ply::compressed {
namespace {

// Canonical property order. Chunk bounds pair as (min, max) at (k, k + 3)
// within each group of six: position, log scale, colour.
constexpr const char* kChunkNames[kChunkPropertyCount] = {
    "min_x",       "min_y",       "min_z",       "max_x",       "max_y",
    "max_z",       "min_scale_x", "min_scale_y", "min_scale_z", "max_scale_x",
    "max_scale_y", "max_scale_z", "min_r",       "min_g",       "min_b",
    "max_r",       "max_g",       "max_b",
};
// The first twelve names alone form the colourless variant SplatTransform's
// reader accepts and no observed producer writes.
constexpr std::size_t kColourlessChunkPropertyCount = 12;

constexpr const char* kVertexNames[kVertexPropertyCount] = {
    "packed_position",
    "packed_rotation",
    "packed_scale",
    "packed_color",
};
enum VertexColumn { kPosition = 0, kRotation = 1, kScale = 2, kColor = 3 };

constexpr double kShC0 = 0.28209479177387814;

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

const PlyElementDescription* FindElement(const PlyHeader& header,
                                         const char* name) noexcept
{
    for (const PlyElementDescription& element : header.elements) {
        if (element.name == name) {
            return &element;
        }
    }
    return nullptr;
}

// Maps each expected name to its declaration index when the element declares
// exactly those names, once each, as non-list scalars of `type`.
template <std::size_t N>
bool ResolveExactProperties(const PlyElementDescription& element,
                            const char* const (&names)[N],
                            std::size_t expectedCount, PlyScalarType type,
                            std::array<std::size_t, N>* columns)
{
    if (element.properties.size() != expectedCount) {
        return false;
    }
    for (std::size_t k = 0; k < expectedCount; ++k) {
        std::size_t found = element.properties.size();
        for (std::size_t i = 0; i < element.properties.size(); ++i) {
            if (element.properties[i].name == names[k]) {
                if (found != element.properties.size()) {
                    return false; // duplicate
                }
                found = i;
            }
        }
        if (found == element.properties.size() ||
            element.properties[found].isList ||
            element.properties[found].type != type) {
            return false;
        }
        (*columns)[k] = found;
    }
    return true;
}

bool IsColourlessChunk(const PlyElementDescription& chunk)
{
    std::array<std::size_t, kChunkPropertyCount> unused{};
    return ResolveExactProperties(chunk, kChunkNames,
                                  kColourlessChunkPropertyCount,
                                  PlyScalarType::Float32, &unused);
}

bool ParseRestIndex(const std::string& name, std::size_t* index)
{
    static const std::string prefix = "f_rest_";
    if (name.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string suffix = name.substr(prefix.size());
    if (suffix.empty() || suffix.size() > 4 ||
        (suffix.size() > 1 && suffix[0] == '0') ||
        !std::all_of(suffix.begin(), suffix.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    *index = static_cast<std::size_t>(std::stoul(suffix));
    return true;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::string Trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// The splat-model tag rule SplatTransform applies to PLY comments: Brush's
// `SplatRenderMode: default | mip | 2dgs` and Postshot's
// `postshot.anti_aliasing=0 | 1`, matched case-insensitively, last match
// wins, unrecognized values ignored. Returns "" for the default model.
std::string RenderModeFromComments(const std::vector<std::string>& comments)
{
    static const std::string modeKey = "splatrendermode:";
    static const std::string aaKey = "postshot.anti_aliasing=";
    std::string mode;
    for (const std::string& comment : comments) {
        const std::string lower = Lower(Trim(comment));
        if (lower.compare(0, modeKey.size(), modeKey) == 0) {
            const std::string value = Trim(lower.substr(modeKey.size()));
            if (value == "default") {
                mode.clear();
            } else if (value == "mip" || value == "2dgs") {
                mode = value;
            }
        } else if (lower.compare(0, aaKey.size(), aaKey) == 0) {
            const std::string value = Trim(lower.substr(aaKey.size()));
            if (value == "1") {
                mode = "mip";
            } else if (value == "0") {
                mode.clear();
            }
        }
    }
    return mode;
}

std::uint32_t LoadU32(const unsigned char* bytes) noexcept
{
    std::uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

float LoadF32(const unsigned char* bytes) noexcept
{
    float value = 0.0f;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

double Unorm(std::uint32_t value, unsigned bits) noexcept
{
    const std::uint32_t mask = (1u << bits) - 1u;
    return static_cast<double>(value & mask) / static_cast<double>(mask);
}

// Exact at both ends (t = 0 gives min, t = 1 gives max).
double Lerp(float minimum, float maximum, double t) noexcept
{
    return static_cast<double>(minimum) * (1.0 - t) +
           static_cast<double>(maximum) * t;
}

// 11/10/11 bits in x/y/z order from bit 31 down.
void Unpack111011(std::uint32_t value, double* x, double* y, double* z) noexcept
{
    *x = Unorm(value >> 21, 11);
    *y = Unorm(value >> 11, 10);
    *z = Unorm(value, 11);
}

// Bucket centres of the writer's trunc((v / 8 + 0.5) * 256), with the two
// end codes pinned to the range ends.
float DecodeShByte(unsigned char byte) noexcept
{
    if (byte == 0) {
        return -4.0f;
    }
    if (byte == 255) {
        return 4.0f;
    }
    return static_cast<float>((static_cast<double>(byte) + 0.5) / 32.0 - 4.0);
}

void Release(std::vector<unsigned char>* bytes) noexcept
{
    std::vector<unsigned char>().swap(*bytes);
}

} // namespace

bool HasCompressedSignature(const PlyHeader& header) noexcept
{
    if (!FindElement(header, "chunk")) {
        return false;
    }
    for (const PlyPropertyDescription& property : header.vertexProperties) {
        if (property.name == "packed_position") {
            return true;
        }
    }
    return false;
}

bool ValidateLayout(const PlyHeader& header, CompressedLayout* layout,
                    std::string* error)
{
    if (!layout) {
        SetError(error, diag::kInternalError,
                 "Compressed PLY validation received a null layout output.");
        return false;
    }
    if (header.encoding != PlyEncoding::BinaryLittleEndian) {
        SetError(error, diag::kCompressedUnsupportedEncoding,
                 std::string("Compressed PLY must be binary_little_endian; "
                             "this file is ") +
                     (header.encoding == PlyEncoding::Ascii ?
                          "ascii." :
                          "binary_big_endian."));
        return false;
    }

    std::map<std::string, std::size_t> elementCounts;
    for (const PlyElementDescription& element : header.elements) {
        if (element.name != "chunk" && element.name != "vertex" &&
            element.name != "sh") {
            SetError(error, diag::kCompressedLayoutMismatch,
                     "Compressed PLY declares unexpected element '" +
                         element.name + "'.");
            return false;
        }
        if (++elementCounts[element.name] > 1) {
            SetError(error, diag::kCompressedLayoutMismatch,
                     "Compressed PLY declares element '" + element.name +
                         "' more than once.");
            return false;
        }
    }
    const PlyElementDescription* chunk = FindElement(header, "chunk");
    const PlyElementDescription* vertex = FindElement(header, "vertex");
    const PlyElementDescription* sh = FindElement(header, "sh");
    if (!chunk || !vertex) {
        SetError(error, diag::kCompressedLayoutMismatch,
                 "Compressed PLY needs both a chunk and a vertex element.");
        return false;
    }

    CompressedLayout result;
    if (IsColourlessChunk(*chunk)) {
        SetError(error, diag::kCompressedUnsupportedVariant,
                 "Compressed PLY uses the 12-property chunk without colour "
                 "bounds; only the 18-property chunk is supported.");
        return false;
    }
    if (!ResolveExactProperties(*chunk, kChunkNames, kChunkPropertyCount,
                                PlyScalarType::Float32, &result.chunkColumn)) {
        SetError(error, diag::kCompressedLayoutMismatch,
                 "Compressed PLY chunk element must declare exactly the 18 "
                 "float bound properties (min_x ... max_b).");
        return false;
    }
    if (!ResolveExactProperties(*vertex, kVertexNames, kVertexPropertyCount,
                                PlyScalarType::UInt32, &result.vertexColumn)) {
        SetError(error, diag::kCompressedLayoutMismatch,
                 "Compressed PLY vertex element must declare exactly the four "
                 "uint properties packed_position, packed_rotation, "
                 "packed_scale, and packed_color.");
        return false;
    }

    const std::size_t count = vertex->count;
    if (count == 0) {
        SetError(error, diag::kEmptyVertexElement,
                 "Compressed PLY vertex element contains no particles.");
        return false;
    }
    if (!IsGaussianCountWithinLimit(count)) {
        SetError(error, diag::kImportLimitExceeded,
                 "Compressed PLY declares " + std::to_string(count) +
                     " Gaussians, above the shared import limit of " +
                     std::to_string(kMaxGaussianCount) + ".");
        return false;
    }
    const std::size_t expectedChunks = (count + kChunkSize - 1) / kChunkSize;
    if (chunk->count != expectedChunks) {
        SetError(error, diag::kCompressedRowCountMismatch,
                 "Compressed PLY declares " + std::to_string(chunk->count) +
                     " chunk rows; " + std::to_string(count) +
                     " Gaussians need " + std::to_string(expectedChunks) + ".");
        return false;
    }

    if (sh) {
        const std::size_t restCount = sh->properties.size();
        result.shColumn.assign(restCount, restCount);
        for (std::size_t i = 0; i < restCount; ++i) {
            const PlyPropertyDescription& property = sh->properties[i];
            std::size_t index = 0;
            if (!ParseRestIndex(property.name, &index) || index >= restCount ||
                result.shColumn[index] != restCount || property.isList ||
                property.type != PlyScalarType::UInt8) {
                SetError(error, diag::kCompressedLayoutMismatch,
                         "Compressed PLY sh element must declare uchar "
                         "properties f_rest_0 ... f_rest_{R-1} exactly once "
                         "each; '" +
                             property.name + "' does not fit.");
                return false;
            }
            result.shColumn[index] = i;
        }
        if (restCount == 0) {
            SetError(error, diag::kCompressedLayoutMismatch,
                     "Compressed PLY sh element declares no properties.");
            return false;
        }
        if (restCount % 3 != 0) {
            SetError(error, diag::kRestCountNotRgb,
                     "Compressed PLY sh property count is not divisible by "
                     "RGB.");
            return false;
        }
        int degree = 0;
        if (!InferShDegree(restCount / 3 + 1, &degree)) {
            SetError(error, diag::kInvalidShDegree,
                     "Compressed PLY sh property count does not form a valid "
                     "degree.");
            return false;
        }
        if (degree > kMaxShDegree) {
            SetError(error, diag::kUnsupportedShDegree,
                     "Compressed PLY SH degree " + std::to_string(degree) +
                         " is not supported; supported degrees are 0-" +
                         std::to_string(kMaxShDegree) + ".");
            return false;
        }
        if (sh->count != count) {
            SetError(error, diag::kCompressedRowCountMismatch,
                     "Compressed PLY declares " + std::to_string(sh->count) +
                         " sh rows for " + std::to_string(count) +
                         " Gaussians.");
            return false;
        }
        result.shDegree = degree;
        result.restPerChannel = restCount / 3;
        for (const PlyPropertyDescription& property : sh->properties) {
            result.shProperties.push_back(property.name);
        }
    }

    for (const PlyPropertyDescription& property : chunk->properties) {
        result.chunkProperties.push_back(property.name);
    }
    for (const PlyPropertyDescription& property : vertex->properties) {
        result.vertexProperties.push_back(property.name);
    }
    result.gaussianCount = count;
    result.chunkCount = expectedChunks;
    result.renderMode = RenderModeFromComments(header.comments);
    *layout = std::move(result);
    return true;
}

bool Decode(const std::string& path, const CompressedLayout& layout,
            GaussianCloudData* cloud, std::vector<std::string>* warnings,
            std::string* error, double* readSeconds)
{
    if (!cloud) {
        SetError(error, diag::kInternalError,
                 "Compressed PLY decoder received a null cloud output.");
        return false;
    }

    std::vector<PlyElementRows> rows(layout.restPerChannel > 0 ? 3 : 2);
    rows[0].element = "chunk";
    rows[0].properties = layout.chunkProperties;
    rows[1].element = "vertex";
    rows[1].properties = layout.vertexProperties;
    if (layout.restPerChannel > 0) {
        rows[2].element = "sh";
        rows[2].properties = layout.shProperties;
    }

    PlyHeader readHeader;
    {
        const auto start = std::chrono::steady_clock::now();
        std::string readerError;
        const bool ok =
            PlyReader().ReadElementRows(path, &rows, &readHeader, &readerError);
        if (readSeconds) {
            *readSeconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - start)
                               .count();
        }
        if (!ok) {
            SetError(error, diag::kUnreadableContainer, readerError);
            return false;
        }
    }
    // The file may have changed between the header-only validation and the
    // read; trust only rows whose header describes the same layout.
    CompressedLayout readLayout;
    if (!ValidateLayout(readHeader, &readLayout, error)) {
        return false;
    }
    if (readLayout.gaussianCount != layout.gaussianCount ||
        readLayout.restPerChannel != layout.restPerChannel ||
        readLayout.chunkProperties != layout.chunkProperties ||
        readLayout.vertexProperties != layout.vertexProperties ||
        readLayout.shProperties != layout.shProperties) {
        SetError(error, diag::kUnreadableContainer,
                 "Compressed PLY header changed while the file was read.");
        return false;
    }

    const std::size_t count = layout.gaussianCount;
    PlyElementRows& chunkRows = rows[0];
    PlyElementRows& vertexRows = rows[1];

    // Chunk bounds, canonical order, validated before any vertex uses them.
    std::vector<std::array<float, kChunkPropertyCount>> chunks;
    if (!TryResize(&chunks, layout.chunkCount)) {
        SetError(error, diag::kModelAllocationFailed,
                 "Compressed PLY chunk table could not be allocated.");
        return false;
    }
    for (std::size_t c = 0; c < layout.chunkCount; ++c) {
        const unsigned char* row =
            chunkRows.bytes.data() + c * chunkRows.rowStride;
        for (std::size_t k = 0; k < kChunkPropertyCount; ++k) {
            chunks[c][k] = LoadF32(row + layout.chunkColumn[k] * sizeof(float));
            if (!std::isfinite(chunks[c][k])) {
                SetError(error, diag::kCompressedInvalidChunkBounds,
                         "Compressed PLY chunk " + std::to_string(c) +
                             " has a non-finite '" + kChunkNames[k] +
                             "' bound.");
                return false;
            }
        }
        for (std::size_t group = 0; group < kChunkPropertyCount; group += 6) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (chunks[c][group + axis] > chunks[c][group + axis + 3]) {
                    SetError(error, diag::kCompressedInvalidChunkBounds,
                             "Compressed PLY chunk " + std::to_string(c) +
                                 " has '" + kChunkNames[group + axis] +
                                 "' above '" + kChunkNames[group + axis + 3] +
                                 "'.");
                    return false;
                }
            }
        }
    }
    Release(&chunkRows.bytes);

    GaussianCloudData result;
    result.gaussianCount = count;
    result.shDegree = layout.shDegree;
    std::size_t restLength = 0;
    if (!ComputeRestCoefficientCount(count, layout.shDegree, &restLength) ||
        !TryResize(&result.positions, count) ||
        !TryResize(&result.scales, count) ||
        !TryResize(&result.rotations, count) ||
        !TryResize(&result.opacities, count) ||
        !TryResize(&result.dcCoefficients, count) ||
        !TryResize(&result.restCoefficients, restLength)) {
        SetError(error, diag::kModelAllocationFailed,
                 "Compressed PLY model arrays for " + std::to_string(count) +
                     " Gaussians at SH degree " +
                     std::to_string(layout.shDegree) +
                     " could not be allocated.");
        return false;
    }

    const double rotationScale = std::sqrt(2.0);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& b = chunks[i / kChunkSize];
        const unsigned char* row =
            vertexRows.bytes.data() + i * vertexRows.rowStride;
        const auto packed = [&](VertexColumn column) {
            return LoadU32(row +
                           layout.vertexColumn[column] * sizeof(std::uint32_t));
        };

        double tx = 0.0;
        double ty = 0.0;
        double tz = 0.0;
        Unpack111011(packed(kPosition), &tx, &ty, &tz);
        result.positions[i] = {
            static_cast<float>(Lerp(b[0], b[3], tx)),
            static_cast<float>(Lerp(b[1], b[4], ty)),
            static_cast<float>(Lerp(b[2], b[5], tz)),
        };

        Unpack111011(packed(kScale), &tx, &ty, &tz);
        const Float3 logScale = {
            static_cast<float>(Lerp(b[6], b[9], tx)),
            static_cast<float>(Lerp(b[7], b[10], ty)),
            static_cast<float>(Lerp(b[8], b[11], tz)),
        };
        if (!DecodeLogScale(logScale, &result.scales[i])) {
            SetError(error, diag::kLogScaleOverflow,
                     "Compressed PLY scale of Gaussian " + std::to_string(i) +
                         " cannot be converted from log space.");
            return false;
        }

        // Smallest three: the largest component's index in bits 31-30, the
        // other three in ascending index order as 10-bit unorm, and the
        // dropped component non-negative by the writer's sign convention.
        // Components are (rot_0, rot_1, rot_2, rot_3) = (w, x, y, z).
        const std::uint32_t rotation = packed(kRotation);
        const double a = (Unorm(rotation >> 20, 10) - 0.5) * rotationScale;
        const double c1 = (Unorm(rotation >> 10, 10) - 0.5) * rotationScale;
        const double c2 = (Unorm(rotation, 10) - 0.5) * rotationScale;
        const double dropped =
            std::sqrt(std::max(0.0, 1.0 - (a * a + c1 * c1 + c2 * c2)));
        const std::size_t largest = rotation >> 30;
        double q[4] = {};
        const double stored[3] = {a, c1, c2};
        for (std::size_t k = 0, s = 0; k < 4; ++k) {
            q[k] = k == largest ? dropped : stored[s++];
        }
        if (!NormalizeQuaternion(
                {static_cast<float>(q[0]), static_cast<float>(q[1]),
                 static_cast<float>(q[2]), static_cast<float>(q[3])},
                &result.rotations[i])) {
            SetError(error, diag::kInvalidQuaternion,
                     "Compressed PLY contains an invalid quaternion.");
            return false;
        }

        // RGB in bits 31-8 as SH_C0 * f_dc + 0.5 across the chunk colour
        // bounds; opacity in bits 7-0, already physical (post-sigmoid).
        const std::uint32_t color = packed(kColor);
        const auto dc = [&](unsigned shift, std::size_t channel) {
            const double value = Lerp(b[12 + channel], b[15 + channel],
                                      Unorm(color >> shift, 8));
            return static_cast<float>((value - 0.5) / kShC0);
        };
        result.dcCoefficients[i] = {dc(24, 0), dc(16, 1), dc(8, 2)};
        result.opacities[i] = static_cast<float>(Unorm(color, 8));
    }
    Release(&vertexRows.bytes);

    if (layout.restPerChannel > 0) {
        // Channel-major f_rest_{channel * C + k}, as in Graphdeco PLY, so the
        // same transpose reaches the model's coefficient-major order.
        const PlyElementRows& shRows = rows[2];
        const std::size_t perChannel = layout.restPerChannel;
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char* row =
                shRows.bytes.data() + i * shRows.rowStride;
            for (std::size_t k = 0; k < perChannel; ++k) {
                result.restCoefficients[i * perChannel + k] = {
                    DecodeShByte(row[layout.shColumn[k]]),
                    DecodeShByte(row[layout.shColumn[perChannel + k]]),
                    DecodeShByte(row[layout.shColumn[2 * perChannel + k]]),
                };
            }
        }
        Release(&rows[2].bytes);
    }

    // Same raw frame as Graphdeco PLY (right-down-front); the model frame is
    // RUB (ADR 0001).
    FlipYZAxes(&result);

    std::string validationError;
    if (!ValidateGaussianCloud(result, &validationError)) {
        SetError(error, diag::kCloudValidationFailed, validationError);
        return false;
    }

    if (warnings && !layout.renderMode.empty()) {
        warnings->push_back(diag::Format(
            diag::kRenderModeIgnored,
            "The file is tagged with splat render mode '" + layout.renderMode +
                "'; the authored schema carries no render-mode convention, "
                "so the Gaussians are authored as ordinary 3DGS splats."));
    }

    *cloud = std::move(result);
    return true;
}

} // namespace openstrata::gs::ply::compressed
