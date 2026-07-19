// SPDX-License-Identifier: Apache-2.0
#pragma once

// Test-only checker for the invariants docs/reference/GAUSSIAN_MODEL_CONTRACT.md
// requires of *any* decoder, independent of source format.
//
// It lives in gaussianCore rather than in a bundle's tests so that every
// decoder is held to the same code, not to a copy of it that can drift.
// `gaussian-ply` runs it today; `gaussian-spz` runs this same function against
// its own fixtures.
//
// ValidateGaussianCloud is the shared gate the writer calls, but it is a
// backstop. This checker additionally pins the properties the contract states
// in prose, so a decoder change that silently starts emitting log-scales,
// opacity logits, or unnormalized quaternions is caught in a unit test rather
// than in a viewer.
//
// Not checked here, because a cloud alone does not carry the evidence:
// SH *ordering*. Gaussian-major and channel-major layouts have identical array
// lengths, so distinguishing them requires known expected values. Each bundle
// verifies ordering against a fixture with hand-computed coefficients (for PLY,
// TestShLayout).

#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianMath.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace openstrata::gs::testing {

// Returns one message per violated rule; an empty result means the cloud
// conforms. Returning messages rather than asserting keeps the checker
// independent of any bundle's test harness.
inline std::vector<std::string> CheckCloudContract(const GaussianCloudData& cloud)
{
    std::vector<std::string> violations;
    const auto fail = [&violations](std::string message) {
        violations.push_back(std::move(message));
    };

    std::string validationError;
    if (!ValidateGaussianCloud(cloud, &validationError)) {
        fail("shared validation failed: " + validationError);
        // Every rule below indexes the arrays, and validation is what
        // establishes that their lengths agree.
        return violations;
    }

    const std::size_t count = cloud.gaussianCount;
    if (count == 0) {
        fail("§3: gaussianCount must be > 0");
        return violations;
    }

    // §3: array lengths are all exactly gaussianCount.
    const auto checkLength = [&](const char* name, std::size_t actual) {
        if (actual != count) {
            fail(std::string("§3: ") + name + " has length " +
                std::to_string(actual) + ", expected " + std::to_string(count));
        }
    };
    checkLength("positions", cloud.positions.size());
    checkLength("scales", cloud.scales.size());
    checkLength("rotations", cloud.rotations.size());
    checkLength("opacities", cloud.opacities.size());
    checkLength("dcCoefficients", cloud.dcCoefficients.size());

    // §3: CoefficientsPerGaussian() includes DC, so rest holds one fewer
    // triple per Gaussian.
    const std::size_t perGaussian = cloud.CoefficientsPerGaussian();
    const std::size_t side = static_cast<std::size_t>(cloud.shDegree + 1);
    if (perGaussian != side * side) {
        fail("§3: CoefficientsPerGaussian() disagrees with shDegree");
    }
    const std::size_t expectedRest = count * (perGaussian - 1);
    if (cloud.restCoefficients.size() != expectedRest) {
        fail("§3: restCoefficients has length " +
            std::to_string(cloud.restCoefficients.size()) + ", expected " +
            std::to_string(expectedRest));
    }

    const auto finite3 = [](const Float3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };

    for (std::size_t i = 0; i < count; ++i) {
        const std::string at = " at Gaussian " + std::to_string(i);

        if (!finite3(cloud.positions[i])) {
            fail("§3: non-finite position" + at);
        }

        // §3: scales are linear and strictly positive, never log-encoded. A
        // log-scale regression shows up as a non-positive value here.
        const Float3& scale = cloud.scales[i];
        if (!finite3(scale)) {
            fail("§3: non-finite scale" + at);
        } else if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f) {
            fail("§3: scale is not strictly positive" + at +
                " (log-encoded scales reaching the model?)");
        }

        // §3: opacity is already through sigmoid, never a logit.
        const float opacity = cloud.opacities[i];
        if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f) {
            fail("§3: opacity outside [0, 1]" + at +
                " (a logit reaching the model?)");
        }

        // §3: quaternions reach the model normalized.
        const Quaternion& q = cloud.rotations[i];
        const float norm = std::sqrt(
            q.real * q.real + q.i * q.i + q.j * q.j + q.k * q.k);
        if (!std::isfinite(norm) || std::fabs(norm - 1.0f) > 1.0e-4f) {
            fail("§3: quaternion is not normalized" + at);
        }

        if (!finite3(cloud.dcCoefficients[i])) {
            fail("§3: non-finite DC coefficient" + at);
        }
    }

    for (std::size_t i = 0; i < cloud.restCoefficients.size(); ++i) {
        if (!finite3(cloud.restCoefficients[i])) {
            fail("§3: non-finite rest coefficient at index " +
                std::to_string(i));
        }
    }

    return violations;
}

} // namespace openstrata::gs::testing
