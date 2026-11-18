// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSplatDecoder.h"

#include "io/GaussianSplatDiagnostics.h"
#include "io/SplatReader.h"
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/GaussianSizeMath.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace openstrata::gs::splat {
namespace {

// The band-0 SH basis constant writers use to turn DC into a displayable
// colour, `0.5 + kShC0 * dc` (SPLAT_MAPPING.md §3).
constexpr float kShC0 = 0.28209479177387814f;

// Record field offsets (SPLAT_FORMAT.md §2).
constexpr std::size_t kPositionOffset = 0;
constexpr std::size_t kScaleOffset = 12;
constexpr std::size_t kColorOffset = 24;
constexpr std::size_t kAlphaOffset = 27;
constexpr std::size_t kRotationOffset = 28;

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

// A little-endian float32, assembled from bytes so the decode does not depend
// on the host byte order or on the record's alignment.
float LoadFloat32(const unsigned char* bytes) noexcept
{
    const std::uint32_t bits = static_cast<std::uint32_t>(bytes[0]) |
                               (static_cast<std::uint32_t>(bytes[1]) << 8) |
                               (static_cast<std::uint32_t>(bytes[2]) << 16) |
                               (static_cast<std::uint32_t>(bytes[3]) << 24);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

Float3 LoadFloat3(const unsigned char* bytes) noexcept
{
    return {LoadFloat32(bytes), LoadFloat32(bytes + 4), LoadFloat32(bytes + 8)};
}

bool IsFinite(const Float3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

// The reference reader's inverse of every writer's `q * 128 + 128`
// (SPLAT_MAPPING.md §4).
float DecodeRotationByte(unsigned char stored) noexcept
{
    return (static_cast<float>(stored) - 128.0f) / 128.0f;
}

float DecodeDc(unsigned char stored) noexcept
{
    return (static_cast<float>(stored) / 255.0f - 0.5f) / kShC0;
}

} // namespace

bool GaussianSplatDecoder::CanRead(const std::string& path) const noexcept
{
    return SplatReader().CanRead(path);
}

bool GaussianSplatDecoder::DecodeMetadata(const std::string& path,
                                          GaussianSplatMetadata* metadata,
                                          std::string* error) const
{
    if (!metadata) {
        SetError(error, diag::kInternalError,
                 "Gaussian decoder received a null metadata output.");
        return false;
    }

    SplatLayout layout;
    if (!SplatReader().ReadLayout(path, &layout, error)) {
        return false;
    }
    metadata->gaussianCount = layout.recordCount;
    metadata->shDegree = 0;
    return true;
}

bool GaussianSplatDecoder::Decode(const std::string& path,
                                  GaussianCloudData* cloud,
                                  std::vector<std::string>* warnings,
                                  std::string* error,
                                  GaussianImportStats* stats) const
{
    if (!cloud) {
        SetError(error, diag::kInternalError,
                 "Gaussian decoder received a null cloud output.");
        return false;
    }

    using Clock = std::chrono::steady_clock;
    const auto seconds = [](Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration<double>(to - from).count();
    };
    const auto readStart = Clock::now();

    SplatDocument document;
    if (!SplatReader().Read(path, &document, error)) {
        return false;
    }
    const auto decodeStart = Clock::now();

    const std::size_t count = document.layout.recordCount;
    // The reader guarantees the buffer size; a disagreement here is pipeline
    // misuse, not file content.
    if (count == 0 || document.records.size() != count * kRecordBytes) {
        SetError(error, diag::kInternalError,
                 "The record buffer does not match the record count.");
        return false;
    }

    GaussianCloudData result;
    result.gaussianCount = count;
    result.shDegree = 0;

    // Model allocations go through the shared overflow-checked helper
    // (GAUSSIAN_MODEL_CONTRACT.md §3): a failed allocation is a decode
    // failure, never a partial cloud.
    const auto allocate = [&](auto* array) {
        if (!TryResize(array, count)) {
            SetError(error, diag::kModelAllocationFailed,
                     "Model arrays for " + std::to_string(count) +
                         " Gaussians could not be allocated.");
            return false;
        }
        return true;
    };
    if (!allocate(&result.positions) || !allocate(&result.scales) ||
        !allocate(&result.rotations) || !allocate(&result.opacities) ||
        !allocate(&result.dcCoefficients)) {
        return false;
    }

    std::size_t identityRotations = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char* record =
            document.records.data() + i * kRecordBytes;

        const Float3 position = LoadFloat3(record + kPositionOffset);
        if (!IsFinite(position)) {
            SetError(error, diag::kNonFinitePosition,
                     "The position of Gaussian " + std::to_string(i) +
                         " is not finite.");
            return false;
        }
        result.positions[i] = position;

        // Scales are stored linear, as exp() of the training log-scale, so a
        // valid writer never produces zero, a negative value, or a
        // non-finite value.
        const Float3 scale = LoadFloat3(record + kScaleOffset);
        if (!IsFinite(scale) || !(scale.x > 0.0f) || !(scale.y > 0.0f) ||
            !(scale.z > 0.0f)) {
            SetError(error, diag::kInvalidScale,
                     "The scale of Gaussian " + std::to_string(i) +
                         " is not a finite positive value.");
            return false;
        }
        result.scales[i] = scale;

        const unsigned char* color = record + kColorOffset;
        result.dcCoefficients[i] = {DecodeDc(color[0]), DecodeDc(color[1]),
                                    DecodeDc(color[2])};
        // Alpha is sigmoid(opacity) * 255: already an opacity in [0, 1].
        result.opacities[i] = static_cast<float>(record[kAlphaOffset]) / 255.0f;

        // Stored scalar-first (w, x, y, z), the model's own order. Bytes of
        // 128 in every component decode to a zero quaternion, which the shared
        // helper replaces with identity.
        const unsigned char* rotation = record + kRotationOffset;
        bool replaced = false;
        if (!NormalizeQuaternion({DecodeRotationByte(rotation[0]),
                                  DecodeRotationByte(rotation[1]),
                                  DecodeRotationByte(rotation[2]),
                                  DecodeRotationByte(rotation[3])},
                                 &result.rotations[i], &replaced)) {
            SetError(error, diag::kInternalError,
                     "A decoded rotation was not normalizable.");
            return false;
        }
        if (replaced) {
            ++identityRotations;
        }
    }

    // Graphdeco PLY frame (RDF) to the model frame (RUB), ADR 0001. There are
    // no rest coefficients, so only positions and rotations change.
    FlipYZAxes(&result);

    std::string validationError;
    if (!ValidateGaussianCloud(result, &validationError)) {
        SetError(error, diag::kCloudValidationFailed, validationError);
        return false;
    }

    if (warnings && identityRotations != 0) {
        warnings->push_back(diag::Format(
            diag::kZeroRotationReplaced,
            std::to_string(identityRotations) +
                " Gaussian(s) stored a zero rotation (all four bytes 128) and "
                "were given the identity orientation."));
    }

    if (stats) {
        stats->sourceFormat = kSourceFormatToken;
        stats->sourceVersion.clear();
        stats->coordinateConversion = GaussianCoordinateConversion::RdfToRub;
        stats->gaussianCount = result.gaussianCount;
        stats->shDegree = result.shDegree;
        stats->warningCount = warnings ? warnings->size() : 0;
        stats->sourceBytes = document.layout.fileBytes;
        stats->decodedBytes = ComputeDecodedByteSize(result);
        stats->readSeconds = seconds(readStart, decodeStart);
        stats->decodeSeconds = seconds(decodeStart, Clock::now());
    }

    *cloud = std::move(result);
    return true;
}

} // namespace openstrata::gs::splat
