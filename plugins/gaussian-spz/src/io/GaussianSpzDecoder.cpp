// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSpzDecoder.h"

#include "io/GaussianSpzDiagnostics.h"
#include "io/SpzReader.h"
#include "openstrata/gs/GaussianMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace openstrata::gs::spz {
namespace {

// The shared model carries SH degrees 0-3 (GAUSSIAN_MODEL_CONTRACT.md §3).
// Degree 4 is valid SPZ and passes the container stage, so the rejection here
// must say "not supported", never "malformed" (SPZ_FORMAT.md §7).
constexpr int kModelMaxShDegree = 3;

// Dequantization constants from the reference serializer, recorded in
// SPZ_MAPPING.md §3: the DC color scale and the smallest-three component
// bound 1/sqrt(2).
constexpr float kColorScale = 0.15f;
constexpr float kSqrt1_2 = 0.70710678118654752440f;

// SPZ conventionally stores right-up-back; the model's reference frame is the
// PLY-native right-down-front (GAUSSIAN_MODEL_CONTRACT.md §2), so decoding
// negates the Y and Z axes. For that axis pair the quaternion flip equals the
// position flip (SPZ_MAPPING.md §4).
constexpr float kFlipY = -1.0f;
constexpr float kFlipZ = -1.0f;

// Sign flips the Y/Z negation induces on the rest SH coefficients, indexed by
// rest coefficient 0-14 (bands 1-3): a real SH basis function changes sign
// when it is odd in an odd number of the flipped axes. The table is the
// reference implementation's flipSh evaluated at (x,y,z) = (+1,-1,-1) and is
// derived in SPZ_MAPPING.md §5.
constexpr float kShFlipRubToRdf[15] = {
    -1.0f, -1.0f, +1.0f,                       // band 1: y, z, x
    -1.0f, +1.0f, +1.0f, -1.0f, +1.0f,         // band 2: xy, yz, zz, xz, xx-yy
    -1.0f, +1.0f, -1.0f, -1.0f, +1.0f, -1.0f,  // band 3
    +1.0f,
};

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

bool CheckSupportedShDegree(std::uint8_t shDegree, std::string* error)
{
    if (static_cast<int>(shDegree) <= kModelMaxShDegree) {
        return true;
    }
    SetError(error, diag::kUnsupportedShDegree,
        "SPZ SH degree " + std::to_string(static_cast<int>(shDegree)) +
        " is valid for the format but not supported by this release; "
        "supported degrees are 0-" + std::to_string(kModelMaxShDegree) + ".");
    return false;
}

// IEEE 754 binary16 -> binary32. The container reader never interprets
// payload bytes, so the v1 position path is the one place a non-finite value
// can enter the pipeline; the caller checks the result.
float HalfToFloat(std::uint16_t half) noexcept
{
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1fu;
    std::uint32_t mantissa = half & 0x3ffu;

    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            // Subnormal half: renormalize into the float32 exponent range.
            std::uint32_t shift = 0;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                ++shift;
            }
            bits = sign | ((113u - shift) << 23) | ((mantissa & 0x3ffu) << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13); // infinity or NaN
    } else {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }

    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// 24-bit little-endian two's-complement fixed point with `fractionalBits`
// fractional bits. The field is an unrestricted byte in the container; ldexp
// underflows large values to zero instead of shifting into undefined
// behavior.
float Fixed24ToFloat(const unsigned char* bytes, int fractionalBits) noexcept
{
    std::int32_t fixed =
        static_cast<std::int32_t>(bytes[0]) |
        (static_cast<std::int32_t>(bytes[1]) << 8) |
        (static_cast<std::int32_t>(bytes[2]) << 16);
    if ((fixed & 0x800000) != 0) {
        fixed |= ~0xffffff;
    }
    return std::ldexp(static_cast<float>(fixed), -fractionalBits);
}

float UnquantizeSh(unsigned char stored) noexcept
{
    return (static_cast<float>(stored) - 128.0f) / 128.0f;
}

// First-three encoding (v1-v2): x, y, z mapped from [0,255] back to [-1,1];
// w reconstructed non-negative from the unit constraint. Quantization can
// push |xyz| slightly past 1, so the radicand is clamped and the result is
// normalized by the caller.
void UnpackRotationFirstThree(const unsigned char* r, float (&q)[4]) noexcept
{
    for (int component = 0; component < 3; ++component) {
        q[component] =
            static_cast<float>(r[component]) / 127.5f - 1.0f;
    }
    const float squaredNorm =
        q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
    q[3] = std::sqrt(std::max(0.0f, 1.0f - squaredNorm));
}

// Smallest-three encoding (v3): bits 30-31 hold the index of the omitted
// (largest, non-negative) component; below them, three 10-bit fields of
// 1 sign + 9 magnitude bits scaled by 1/sqrt(2), packed so the lowest bits
// hold the highest-indexed stored component. The radicand clamp mirrors the
// first-three path; the reference implementation omits it and produces NaN
// for hostile magnitudes.
void UnpackRotationSmallestThree(const unsigned char* r, float (&q)[4]) noexcept
{
    std::uint32_t packed =
        static_cast<std::uint32_t>(r[0]) |
        (static_cast<std::uint32_t>(r[1]) << 8) |
        (static_cast<std::uint32_t>(r[2]) << 16) |
        (static_cast<std::uint32_t>(r[3]) << 24);

    constexpr std::uint32_t kMagnitudeMask = (1u << 9) - 1u;
    const int largest = static_cast<int>(packed >> 30);
    float squaredNorm = 0.0f;
    for (int component = 3; component >= 0; --component) {
        if (component == largest) {
            continue;
        }
        const std::uint32_t magnitude = packed & kMagnitudeMask;
        const bool negative = ((packed >> 9) & 1u) != 0;
        packed >>= 10;
        float value = kSqrt1_2 * static_cast<float>(magnitude) /
            static_cast<float>(kMagnitudeMask);
        if (negative) {
            value = -value;
        }
        q[component] = value;
        squaredNorm += value * value;
    }
    q[largest] = std::sqrt(std::max(0.0f, 1.0f - squaredNorm));
}

} // namespace

bool GaussianSpzDecoder::CanRead(const std::string& path) const noexcept
{
    return SpzReader().CanRead(path);
}

bool GaussianSpzDecoder::DecodeMetadata(
    const std::string& path,
    GaussianSpzMetadata* metadata,
    std::string* error) const
{
    if (!metadata) {
        SetError(error, diag::kInternalError,
            "Gaussian decoder received a null metadata output.");
        return false;
    }

    SpzHeader header;
    if (!SpzReader().ReadHeader(path, &header, error)) {
        return false;
    }
    if (!CheckSupportedShDegree(header.shDegree, error)) {
        return false;
    }

    metadata->gaussianCount = header.pointCount;
    metadata->shDegree = static_cast<int>(header.shDegree);
    return true;
}

bool GaussianSpzDecoder::Decode(
    const std::string& path,
    GaussianCloudData* cloud,
    std::vector<std::string>* warnings,
    std::string* error) const
{
    if (!cloud) {
        SetError(error, diag::kInternalError,
            "Gaussian decoder received a null cloud output.");
        return false;
    }

    SpzPackedDocument document;
    if (!SpzReader().Read(path, &document, error)) {
        return false;
    }
    const SpzHeader& header = document.header;
    if (!CheckSupportedShDegree(header.shDegree, error)) {
        return false;
    }

    const std::size_t count = header.pointCount;
    const std::size_t restDims = header.ShDimensions();

    // The reader guarantees the span layout; a disagreement here is pipeline
    // misuse, not file content.
    if (document.positions.size != count * header.BytesPerPosition() ||
        document.alphas.size != count ||
        document.colors.size != count * 3 ||
        document.scales.size != count * 3 ||
        document.rotations.size != count * header.BytesPerRotation() ||
        document.sh.size != count * 3 * restDims ||
        document.sh.offset + document.sh.size != document.payload.size()) {
        SetError(error, diag::kInternalError,
            "The container spans do not match the header layout.");
        return false;
    }

    GaussianCloudData result;
    result.gaussianCount = count;
    result.shDegree = static_cast<int>(header.shDegree);

    {
        const unsigned char* stored = document.Bytes(document.positions);
        result.positions.resize(count);
        if (header.version == 1) {
            for (std::size_t i = 0; i < count; ++i) {
                float decoded[3];
                for (int axis = 0; axis < 3; ++axis) {
                    const unsigned char* h = stored + i * 6 + axis * 2;
                    decoded[axis] = HalfToFloat(
                        static_cast<std::uint16_t>(
                            h[0] | (static_cast<std::uint16_t>(h[1]) << 8)));
                }
                if (!std::isfinite(decoded[0]) || !std::isfinite(decoded[1]) ||
                    !std::isfinite(decoded[2])) {
                    SetError(error, diag::kNonFinitePosition,
                        "The float16 position of Gaussian " +
                        std::to_string(i) + " is not finite.");
                    return false;
                }
                result.positions[i] = {
                    decoded[0], kFlipY * decoded[1], kFlipZ * decoded[2]};
            }
        } else {
            const int fractionalBits =
                static_cast<int>(header.fractionalBits);
            for (std::size_t i = 0; i < count; ++i) {
                const unsigned char* p = stored + i * 9;
                result.positions[i] = {
                    Fixed24ToFloat(p, fractionalBits),
                    kFlipY * Fixed24ToFloat(p + 3, fractionalBits),
                    kFlipZ * Fixed24ToFloat(p + 6, fractionalBits)};
            }
        }
    }

    {
        // Stored alpha is sigmoid(logit) * 255: already an opacity, only the
        // byte scale to undo. Always in [0, 1].
        const unsigned char* stored = document.Bytes(document.alphas);
        result.opacities.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            result.opacities[i] = static_cast<float>(stored[i]) / 255.0f;
        }
    }

    {
        // Log-encoded byte scales: exp(s/16 - 10) is strictly positive and
        // finite over the whole byte range, so no per-value failure exists.
        const unsigned char* stored = document.Bytes(document.scales);
        result.scales.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto decode = [&](std::size_t component) {
                return std::exp(
                    static_cast<float>(stored[i * 3 + component]) / 16.0f -
                    10.0f);
            };
            result.scales[i] = {decode(0), decode(1), decode(2)};
        }
    }

    {
        const unsigned char* stored = document.Bytes(document.colors);
        result.dcCoefficients.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto decode = [&](std::size_t channel) {
                return (static_cast<float>(stored[i * 3 + channel]) / 255.0f -
                        0.5f) / kColorScale;
            };
            result.dcCoefficients[i] = {decode(0), decode(1), decode(2)};
        }
    }

    {
        const unsigned char* stored = document.Bytes(document.rotations);
        const std::size_t stride = header.BytesPerRotation();
        result.rotations.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            // Decoded as SPZ stores it: vector-first (x, y, z, w).
            float q[4];
            if (header.version >= 3) {
                UnpackRotationSmallestThree(stored + i * stride, q);
            } else {
                UnpackRotationFirstThree(stored + i * stride, q);
            }
            q[1] *= kFlipY;
            q[2] *= kFlipZ;

            // Reorder to the model's scalar-first convention and absorb the
            // quantization drift; the decoded norm is never near zero
            // (SPZ_MAPPING.md §4), so identity replacement is unreachable.
            if (!NormalizeQuaternion(
                    {q[3], q[0], q[1], q[2]}, &result.rotations[i])) {
                SetError(error, diag::kInternalError,
                    "A dequantized quaternion was not normalizable.");
                return false;
            }
        }
    }

    if (restDims != 0) {
        const unsigned char* stored = document.Bytes(document.sh);
        result.restCoefficients.resize(count * restDims);
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t coefficient = 0; coefficient < restDims;
                 ++coefficient) {
                // Channel is the inner axis in the stream; the model wants
                // Gaussian-major RGB triples, so the transpose is only a
                // stride change.
                const unsigned char* rgb =
                    stored + (i * restDims + coefficient) * 3;
                const float flip = kShFlipRubToRdf[coefficient];
                result.restCoefficients[i * restDims + coefficient] = {
                    flip * UnquantizeSh(rgb[0]),
                    flip * UnquantizeSh(rgb[1]),
                    flip * UnquantizeSh(rgb[2])};
            }
        }
    }

    std::string validationError;
    if (!ValidateGaussianCloud(result, &validationError)) {
        SetError(error, diag::kCloudValidationFailed, validationError);
        return false;
    }

    if (warnings) {
        if (!document.extensions.empty()) {
            warnings->push_back(diag::Format(diag::kExtensionsIgnored,
                std::to_string(document.extensions.size()) +
                " extension-record byte(s) were ignored; extension records "
                "are not part of the shared Gaussian model."));
        }
        if (header.IsAntialiased()) {
            warnings->push_back(diag::Format(diag::kAntialiasedFlagIgnored,
                "The antialiased flag was ignored; the authored schema does "
                "not carry an antialiasing convention."));
        }
    }

    *cloud = std::move(result);
    return true;
}

} // namespace openstrata::gs::spz
