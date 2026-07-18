// SPDX-License-Identifier: Apache-2.0
#include "openstrata/gs/GaussianMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openstrata::gs {
namespace {

bool IsFinite(const Float3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

void SetError(std::string* error, const char* message) noexcept
{
    if (error) {
        try {
            *error = message;
        } catch (...) {
            // Validation is noexcept; allocation failure cannot improve the
            // diagnostic, but the false return still preserves correctness.
        }
    }
}

} // namespace

float Sigmoid(float value) noexcept
{
    if (value >= 0.0f) {
        const float e = std::exp(-value);
        return 1.0f / (1.0f + e);
    }
    const float e = std::exp(value);
    return e / (1.0f + e);
}

bool DecodeLogScale(const Float3& stored, Float3* actual) noexcept
{
    if (!actual || !IsFinite(stored)) {
        return false;
    }
    *actual = {
        std::exp(stored.x),
        std::exp(stored.y),
        std::exp(stored.z),
    };
    return IsFinite(*actual) && actual->x > 0.0f && actual->y > 0.0f &&
        actual->z > 0.0f;
}

bool NormalizeQuaternion(
    const Quaternion& stored,
    Quaternion* normalized,
    bool* replacedWithIdentity,
    bool* changed) noexcept
{
    if (replacedWithIdentity) {
        *replacedWithIdentity = false;
    }
    if (changed) {
        *changed = false;
    }
    if (!normalized || !std::isfinite(stored.real) ||
        !std::isfinite(stored.i) || !std::isfinite(stored.j) ||
        !std::isfinite(stored.k)) {
        return false;
    }

    const double lengthSquared =
        static_cast<double>(stored.real) * stored.real +
        static_cast<double>(stored.i) * stored.i +
        static_cast<double>(stored.j) * stored.j +
        static_cast<double>(stored.k) * stored.k;
    if (lengthSquared <= 1.0e-20) {
        *normalized = {};
        if (replacedWithIdentity) {
            *replacedWithIdentity = true;
        }
        if (changed) {
            *changed = true;
        }
        return true;
    }

    const float inverseLength =
        static_cast<float>(1.0 / std::sqrt(lengthSquared));
    *normalized = {
        stored.real * inverseLength,
        stored.i * inverseLength,
        stored.j * inverseLength,
        stored.k * inverseLength,
    };
    if (changed) {
        *changed = std::abs(lengthSquared - 1.0) > 1.0e-5;
    }
    return true;
}

bool InferShDegree(std::size_t coefficientCount, int* degree) noexcept
{
    if (!degree || coefficientCount == 0) {
        return false;
    }
    const double root = std::sqrt(static_cast<double>(coefficientCount));
    const std::size_t side = static_cast<std::size_t>(std::llround(root));
    if (side == 0 || side * side != coefficientCount ||
        side - 1 > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    *degree = static_cast<int>(side - 1);
    return true;
}

bool ValidateGaussianCloud(
    const GaussianCloudData& cloud,
    std::string* error) noexcept
{
    const std::size_t count = cloud.gaussianCount;
    if (count == 0) {
        SetError(error, "Gaussian cloud contains no particles.");
        return false;
    }
    if (cloud.positions.size() != count || cloud.scales.size() != count ||
        cloud.rotations.size() != count || cloud.opacities.size() != count ||
        cloud.dcCoefficients.size() != count) {
        SetError(error, "Gaussian attribute array lengths do not match.");
        return false;
    }

    int inferredDegree = 0;
    if (cloud.shDegree < 0 ||
        !InferShDegree(cloud.CoefficientsPerGaussian(), &inferredDegree) ||
        inferredDegree != cloud.shDegree) {
        SetError(error, "Gaussian SH degree is invalid.");
        return false;
    }
    const std::size_t restPerGaussian = cloud.CoefficientsPerGaussian() - 1;
    if (cloud.restCoefficients.size() != count * restPerGaussian) {
        SetError(error, "Gaussian SH coefficient array length does not match.");
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const Quaternion& q = cloud.rotations[i];
        if (!IsFinite(cloud.positions[i]) || !IsFinite(cloud.scales[i]) ||
            cloud.scales[i].x <= 0.0f || cloud.scales[i].y <= 0.0f ||
            cloud.scales[i].z <= 0.0f || !std::isfinite(q.real) ||
            !std::isfinite(q.i) || !std::isfinite(q.j) ||
            !std::isfinite(q.k) || !std::isfinite(cloud.opacities[i]) ||
            cloud.opacities[i] < 0.0f || cloud.opacities[i] > 1.0f ||
            !IsFinite(cloud.dcCoefficients[i])) {
            SetError(error, "Gaussian cloud contains an invalid numeric value.");
            return false;
        }
    }
    for (const Float3& coefficient : cloud.restCoefficients) {
        if (!IsFinite(coefficient)) {
            SetError(error, "Gaussian SH coefficients contain a non-finite value.");
            return false;
        }
    }
    return true;
}

} // namespace openstrata::gs
