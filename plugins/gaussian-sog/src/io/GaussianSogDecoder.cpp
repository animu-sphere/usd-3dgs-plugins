// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSogDecoder.h"

#include "io/GaussianSogDiagnostics.h"
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/GaussianSizeMath.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openstrata::gs::sog {
namespace {

// Dequantization constants from the reference implementation, recorded in
// SOG_MAPPING.md: the 16-bit position code range and the smallest-three
// component bound 1/sqrt(2).
constexpr float kPositionCodeMaximum = 65535.0f;
constexpr float kSqrt1_2 = 0.70710678118654752440f;
// The alpha byte of quats.webp is 252 + the index of the dropped (largest)
// component (write-sog.ts), so the tag occupies the top four byte values.
constexpr unsigned char kQuaternionTagBase = 252;
// Palette centroids are laid out 64 per row (SOG_MAPPING.md §6).
constexpr std::size_t kShCentroidsPerRow = 64;

// Slot order the three stored components fill, per dropped-component index:
// the non-dropped scalar-first (w, x, y, z) slots in ascending order
// (read-sog.ts `QUAT_IDX`).
constexpr std::array<std::array<int, 3>, 4> kQuaternionSlots = {{
    {{1, 2, 3}}, // w dropped
    {{0, 2, 3}}, // x dropped
    {{0, 1, 3}}, // y dropped
    {{0, 1, 2}}, // z dropped
}};

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

// `logTransform(v) = sign(v) * ln(|v| + 1)` is what the encoder applies before
// quantizing positions; this is its exact inverse (SOG_MAPPING.md §4). Finite
// input can still leave float range here — `exp` of a large log-domain bound —
// so the caller checks the result.
float InverseLogTransform(float value) noexcept
{
    const float magnitude = std::exp(std::fabs(value)) - 1.0f;
    return value < 0.0f ? -magnitude : magnitude;
}

} // namespace

GaussianSogDecoder::GaussianSogDecoder(SogReader reader)
    : _reader(std::move(reader))
{
}

bool GaussianSogDecoder::CanReadBundled(const std::string& path) const noexcept
{
    return _reader.CanReadBundled(path);
}

bool GaussianSogDecoder::CanReadUnbundled(const std::string& path) const noexcept
{
    return _reader.CanReadUnbundled(path);
}

bool GaussianSogDecoder::DecodeMetadata(
    const std::string& path,
    GaussianSogMetadata* metadata,
    std::string* error) const
{
    if (!metadata) {
        SetError(error, diag::kInternalError,
            "Gaussian decoder received a null metadata output.");
        return false;
    }

    SogMetadata source;
    if (!_reader.ReadMetadata(path, &source, error)) {
        return false;
    }
    metadata->gaussianCount = source.gaussianCount;
    // SOG v2 bands 1-3 are exactly SH degrees 1-3, and no `shN` is degree 0,
    // so the whole format range sits inside the model's supported degrees --
    // unlike SPZ, no degree-ceiling rejection exists (SOG_MAPPING.md §7).
    metadata->shDegree = source.shBands;
    return true;
}

bool GaussianSogDecoder::Decode(
    const std::string& path,
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

    SogDocument document;
    if (!_reader.Read(path, &document, error)) {
        return false;
    }
    const auto decodeStart = Clock::now();

    const std::size_t count = document.metadata.gaussianCount;
    const int shDegree = document.metadata.shBands;
    // The reader validated presence, dimensions, and `count <= width*height`
    // for every plane, so a disagreement here is pipeline misuse, not file
    // content.
    if (count == 0 || !document.meansLow.Present() ||
        !document.meansHigh.Present() || !document.scales.Present() ||
        !document.quats.Present() || !document.sh0.Present() ||
        document.scalesCodebook.size() != 256 ||
        document.sh0Codebook.size() != 256 ||
        (shDegree != 0 &&
            (!document.shCentroids.Present() || !document.shLabels.Present() ||
             document.shCodebook.size() != 256))) {
        SetError(error, diag::kInternalError,
            "The container document does not match the metadata it declares.");
        return false;
    }

    GaussianCloudData result;
    result.gaussianCount = count;
    result.shDegree = shDegree;

    // Model allocations go through the shared overflow-checked helpers
    // (GAUSSIAN_MODEL_CONTRACT.md §3): a count whose derived sizes overflow or
    // whose allocation fails is a decode failure, never a partial cloud.
    const auto allocate = [&](auto* array, std::size_t elements) {
        if (!TryResize(array, elements)) {
            SetError(error, diag::kModelAllocationFailed,
                "SOG model arrays for " + std::to_string(count) +
                " Gaussians at SH degree " + std::to_string(shDegree) +
                " could not be allocated.");
            return false;
        }
        return true;
    };

    if (!allocate(&result.positions, count) ||
        !allocate(&result.scales, count) ||
        !allocate(&result.rotations, count) ||
        !allocate(&result.opacities, count) ||
        !allocate(&result.dcCoefficients, count)) {
        return false;
    }

    // The scales codebook is log-domain, so every entry is exponentiated once
    // here instead of once per Gaussian. An entry whose exponential leaves
    // float range is only rejected if a Gaussian actually uses it: unused
    // codebook slots must not fail an otherwise sound file.
    std::vector<float> linearScales;
    if (!allocate(&linearScales, document.scalesCodebook.size())) {
        return false;
    }
    for (std::size_t i = 0; i < document.scalesCodebook.size(); ++i) {
        linearScales[i] = std::exp(document.scalesCodebook[i]);
    }

    {
        // Positions: a 16-bit code per axis, split low byte in `means_l` and
        // high byte in `means_u`, remapped through the per-axis log-domain
        // range and inverse-log transformed (SOG_MAPPING.md §4).
        float span[3];
        for (int axis = 0; axis < 3; ++axis) {
            span[axis] = document.meansMaximum[axis] -
                document.meansMinimum[axis];
        }
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char* low = document.meansLow.Gaussian(i);
            const unsigned char* high = document.meansHigh.Gaussian(i);
            float decoded[3];
            for (int axis = 0; axis < 3; ++axis) {
                const std::uint32_t code =
                    static_cast<std::uint32_t>(low[axis]) |
                    (static_cast<std::uint32_t>(high[axis]) << 8);
                const float normalized =
                    document.meansMinimum[axis] +
                    span[axis] * (static_cast<float>(code) /
                        kPositionCodeMaximum);
                decoded[axis] = InverseLogTransform(normalized);
                if (!std::isfinite(decoded[axis])) {
                    SetError(error, diag::kMalformedMetadata,
                        "The position range in meta.json decodes Gaussian " +
                        std::to_string(i) + " outside the range of a 32-bit "
                        "float; \"means\".mins/maxs are log-domain bounds.");
                    return false;
                }
            }
            result.positions[i] = {decoded[0], decoded[1], decoded[2]};
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        // Scales: one codebook index per axis, the codebook log-domain.
        const unsigned char* stored = document.scales.Gaussian(i);
        const float x = linearScales[stored[0]];
        const float y = linearScales[stored[1]];
        const float z = linearScales[stored[2]];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            SetError(error, diag::kInvalidCodebook,
                "A scales codebook entry used by Gaussian " +
                std::to_string(i) + " exponentiates outside the range of a "
                "32-bit float; the codebook is log-domain.");
            return false;
        }
        result.scales[i] = {x, y, z};
    }

    for (std::size_t i = 0; i < count; ++i) {
        // DC and opacity share the sh0 plane: RGB are codebook indices for the
        // raw band-0 coefficients, alpha is the already post-sigmoid opacity
        // (SOG_MAPPING.md §3).
        const unsigned char* stored = document.sh0.Gaussian(i);
        result.dcCoefficients[i] = {
            document.sh0Codebook[stored[0]],
            document.sh0Codebook[stored[1]],
            document.sh0Codebook[stored[2]]};
        result.opacities[i] = static_cast<float>(stored[3]) / 255.0f;
    }

    for (std::size_t i = 0; i < count; ++i) {
        // Rotations: smallest-three, the dropped component named by the alpha
        // tag and reconstructed non-negative (SOG_MAPPING.md §5).
        const unsigned char* stored = document.quats.Gaussian(i);
        if (stored[3] < kQuaternionTagBase) {
            SetError(error, diag::kMalformedRotation,
                "The quaternion of Gaussian " + std::to_string(i) +
                " carries the largest-component tag " +
                std::to_string(static_cast<int>(stored[3])) +
                "; SOG v2 tags are 252-255.");
            return false;
        }
        const int dropped = static_cast<int>(stored[3] - kQuaternionTagBase);
        float components[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float squaredNorm = 0.0f;
        for (int stage = 0; stage < 3; ++stage) {
            const float value =
                (static_cast<float>(stored[stage]) / 255.0f * 2.0f - 1.0f) *
                kSqrt1_2;
            components[kQuaternionSlots[dropped][stage]] = value;
            squaredNorm += value * value;
        }
        // The clamp guards the radicand against quantization pushing the sum
        // of squares past 1; the reference omits it and yields NaN.
        components[dropped] = std::sqrt(std::max(0.0f, 1.0f - squaredNorm));

        // The four slots are the Graphdeco rot_0..rot_3 in scalar-first order,
        // which is the model's own convention, so only the smallest-three
        // unpack is a reordering.
        if (!NormalizeQuaternion(
                {components[0], components[1], components[2], components[3]},
                &result.rotations[i])) {
            SetError(error, diag::kInternalError,
                "A dequantized quaternion was not normalizable.");
            return false;
        }
    }

    std::size_t labelsOutOfRange = 0;
    if (shDegree != 0) {
        // Higher-order SH: a 16-bit palette label per Gaussian, each centroid
        // occupying `coefficients` consecutive texels whose R/G/B are the
        // codebook indices of that coefficient's red/green/blue channel
        // (SOG_MAPPING.md §6).
        const std::size_t coefficients =
            static_cast<std::size_t>(shDegree) *
            (static_cast<std::size_t>(shDegree) + 2);
        std::size_t restLength = 0;
        if (!ComputeRestCoefficientCount(count, shDegree, &restLength)) {
            SetError(error, diag::kModelAllocationFailed,
                "The model size for " + std::to_string(count) +
                " Gaussians at SH degree " + std::to_string(shDegree) +
                " overflows this platform's address space.");
            return false;
        }
        if (!allocate(&result.restCoefficients, restLength)) {
            return false;
        }

        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char* stored = document.shLabels.Gaussian(i);
            const std::size_t label =
                static_cast<std::size_t>(stored[0]) |
                (static_cast<std::size_t>(stored[1]) << 8);
            if (label >= document.shPaletteCount) {
                // The reference decoder's out-of-range guard: no centroid, so
                // no higher-order contribution. Reported once, aggregated.
                ++labelsOutOfRange;
                continue;
            }
            const std::size_t row = label / kShCentroidsPerRow;
            const std::size_t column =
                (label % kShCentroidsPerRow) * coefficients;
            for (std::size_t coefficient = 0; coefficient < coefficients;
                 ++coefficient) {
                const unsigned char* texel = document.shCentroids.Texel(
                    column + coefficient, row);
                result.restCoefficients[i * coefficients + coefficient] = {
                    document.shCodebook[texel[0]],
                    document.shCodebook[texel[1]],
                    document.shCodebook[texel[2]]};
            }
        }
    }

    // SOG stores Graphdeco-PLY-convention columns; the model's reference frame
    // is RUB (ADR 0001, SOG_MAPPING.md §5), so decoding negates the Y and Z
    // axes of positions, rotations, and rest SH coefficients through the same
    // shared helper the PLY decoder uses.
    FlipYZAxes(&result);

    std::string validationError;
    if (!ValidateGaussianCloud(result, &validationError)) {
        SetError(error, diag::kCloudValidationFailed, validationError);
        return false;
    }

    if (warnings && labelsOutOfRange != 0) {
        warnings->push_back(diag::Format(diag::kShLabelsOutOfRange,
            std::to_string(labelsOutOfRange) + " of " + std::to_string(count) +
            " spherical-harmonic palette label(s) point past the " +
            std::to_string(document.shPaletteCount) +
            "-centroid palette; those Gaussians decoded with zero "
            "higher-order coefficients."));
    }

    if (stats) {
        stats->sourceFormat = kSourceFormatToken;
        stats->sourceVersion = std::to_string(document.metadata.version);
        stats->gaussianCount = result.gaussianCount;
        stats->shDegree = result.shDegree;
        stats->sourceBytes = document.sourceBytes;
        stats->decodedBytes = ComputeDecodedByteSize(result);
        stats->readSeconds = seconds(readStart, decodeStart);
        stats->decodeSeconds = seconds(decodeStart, Clock::now());
    }

    *cloud = std::move(result);
    return true;
}

} // namespace openstrata::gs::sog
