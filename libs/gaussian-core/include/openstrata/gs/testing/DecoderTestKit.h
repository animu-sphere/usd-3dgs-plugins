// SPDX-License-Identifier: Apache-2.0
#pragma once

// The decoder test kit (v0.4.0, design policy §7.4): everything a bundle
// needs to test a semantic decoder against GAUSSIAN_MODEL_CONTRACT.md without
// authoring a USD stage.
//
// It lives in gaussianCore's testing headers, next to CloudContract.h, for
// the same reason: every decoder is held to one copy of this code. A decoder
// test built on the kit encodes the canonical clouds below into its own
// format, decodes them back, and requires CompareClouds to return empty --
// which pins Gaussian order, coefficient order, channel order, quaternion
// convention, frame, extent, and every numeric value at once. The invalid
// cases pin the rejection side: each one must fail shared validation, so a
// decoder that "repairs" contract-violating data into the model is caught.
//
// Test-only: nothing here is part of the shipped import path.

#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianMath.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace openstrata::gs::testing {

// --- Canonical expected models ---------------------------------------------
//
// Deterministic clouds in the model's own terms: RUB frame, linear scales,
// sigmoid opacities, normalized scalar-first quaternions, Gaussian-major rest
// coefficients. A decoder test encodes these into its format under the
// format's native conventions (its frame, its quantization, its SH layout)
// and decodes back, so every conversion the contract requires is exercised.
//
// Every rest coefficient value is unique across (gaussian, coefficient,
// channel) by construction -- value = gaussian + coefficient/100 + channel/1000
// -- so any transposition of the three orderings produces a value mismatch,
// not a coincidental pass. Array lengths cannot distinguish those layouts
// (CloudContract.h); these values can.

// One Gaussian, SH degree 0: the smallest conforming cloud.
inline GaussianCloudData MakeCanonicalOneGaussianCloud()
{
    GaussianCloudData cloud;
    cloud.gaussianCount = 1;
    cloud.shDegree = 0;
    cloud.positions.push_back({0.5f, -1.25f, 2.0f});
    cloud.scales.push_back({0.01f, 0.02f, 0.04f});
    // 90 degrees about +X: (cos 45, sin 45, 0, 0).
    cloud.rotations.push_back({0.70710678f, 0.70710678f, 0.0f, 0.0f});
    cloud.opacities.push_back(0.75f);
    cloud.dcCoefficients.push_back({0.25f, -0.5f, 1.0f});
    return cloud;
}

// Three asymmetric Gaussians, SH degree 3: every band, no two values equal.
inline GaussianCloudData MakeCanonicalMultiGaussianCloud()
{
    GaussianCloudData cloud;
    cloud.gaussianCount = 3;
    cloud.shDegree = 3;
    const std::size_t restPerGaussian = cloud.CoefficientsPerGaussian() - 1;
    for (std::size_t g = 0; g < cloud.gaussianCount; ++g) {
        const float f = static_cast<float>(g);
        cloud.positions.push_back({f + 0.5f, -f - 0.25f, 2.0f * f - 1.0f});
        cloud.scales.push_back(
            {0.01f * (f + 1.0f), 0.02f * (f + 1.0f), 0.05f * (f + 1.0f)});
        cloud.opacities.push_back(0.25f * (f + 1.0f));
        cloud.dcCoefficients.push_back(
            {0.1f * (f + 1.0f), -0.2f * (f + 1.0f), 0.3f * (f + 1.0f)});
        for (std::size_t c = 0; c < restPerGaussian; ++c) {
            const float base = f + static_cast<float>(c) / 100.0f;
            cloud.restCoefficients.push_back(
                {base, base + 0.001f, base + 0.002f});
        }
    }
    // Distinct normalized rotations: identity, 90 deg about +X, 120 deg about the
    // main diagonal.
    cloud.rotations.push_back({1.0f, 0.0f, 0.0f, 0.0f});
    cloud.rotations.push_back({0.70710678f, 0.70710678f, 0.0f, 0.0f});
    cloud.rotations.push_back({0.5f, 0.5f, 0.5f, 0.5f});
    return cloud;
}

// --- Comparison -------------------------------------------------------------

// Per-field absolute tolerances. The defaults are tight enough for a lossless
// decoder; a quantizing format derives its own from its documented
// quantization steps, the way tests/equivalence does for SPZ.
struct CloudTolerances {
    float position = 1.0e-6f;
    float scale = 1.0e-6f;
    // Compared through quaternion sign equivalence, per component.
    float rotation = 1.0e-6f;
    float opacity = 1.0e-6f;
    float shCoefficient = 1.0e-6f;
    // Bound for the derived ComputeCloudExtent comparison.
    float extent = 1.0e-5f;
};

// q and -q denote the same rotation and both are admissible
// (GAUSSIAN_MODEL_CONTRACT.md §3), so equality holds under whichever sign
// matches better.
inline bool QuaternionsEquivalent(
    const Quaternion& a, const Quaternion& b, float tolerance) noexcept
{
    const auto close = [tolerance](const Quaternion& p, const Quaternion& q) {
        return std::fabs(p.real - q.real) <= tolerance &&
            std::fabs(p.i - q.i) <= tolerance &&
            std::fabs(p.j - q.j) <= tolerance &&
            std::fabs(p.k - q.k) <= tolerance;
    };
    const Quaternion negated = {-b.real, -b.i, -b.j, -b.k};
    return close(a, b) || close(a, negated);
}

// Returns one message per mismatch; empty means `actual` matches `expected`
// within the tolerances. Compares structure (count, degree, lengths), every
// field per Gaussian, every rest coefficient by (gaussian, coefficient)
// index, and the deterministic derived extent -- without any USD stage.
inline std::vector<std::string> CompareClouds(
    const GaussianCloudData& actual,
    const GaussianCloudData& expected,
    const CloudTolerances& tolerances = {})
{
    std::vector<std::string> mismatches;
    const auto fail = [&mismatches](std::string message) {
        mismatches.push_back(std::move(message));
    };

    if (actual.gaussianCount != expected.gaussianCount) {
        fail("gaussianCount " + std::to_string(actual.gaussianCount) +
            " != expected " + std::to_string(expected.gaussianCount));
        return mismatches;
    }
    if (actual.shDegree != expected.shDegree) {
        fail("shDegree " + std::to_string(actual.shDegree) +
            " != expected " + std::to_string(expected.shDegree));
        return mismatches;
    }
    const auto checkLength = [&](
        const char* name, std::size_t got, std::size_t want) {
        if (got != want) {
            fail(std::string(name) + " has length " + std::to_string(got) +
                ", expected " + std::to_string(want));
            return false;
        }
        return true;
    };
    if (!checkLength("positions", actual.positions.size(),
            expected.positions.size()) ||
        !checkLength("scales", actual.scales.size(),
            expected.scales.size()) ||
        !checkLength("rotations", actual.rotations.size(),
            expected.rotations.size()) ||
        !checkLength("opacities", actual.opacities.size(),
            expected.opacities.size()) ||
        !checkLength("dcCoefficients", actual.dcCoefficients.size(),
            expected.dcCoefficients.size()) ||
        !checkLength("restCoefficients", actual.restCoefficients.size(),
            expected.restCoefficients.size())) {
        return mismatches;
    }

    const auto close = [](float a, float b, float tolerance) {
        return std::fabs(a - b) <= tolerance;
    };
    const auto checkFloat3 = [&](
        const char* name, std::size_t index,
        const Float3& got, const Float3& want, float tolerance) {
        if (!close(got.x, want.x, tolerance) ||
            !close(got.y, want.y, tolerance) ||
            !close(got.z, want.z, tolerance)) {
            fail(std::string(name) + "[" + std::to_string(index) + "] (" +
                std::to_string(got.x) + ", " + std::to_string(got.y) + ", " +
                std::to_string(got.z) + ") != expected (" +
                std::to_string(want.x) + ", " + std::to_string(want.y) +
                ", " + std::to_string(want.z) + ")");
        }
    };

    for (std::size_t i = 0; i < actual.gaussianCount; ++i) {
        checkFloat3("positions", i, actual.positions[i],
            expected.positions[i], tolerances.position);
        checkFloat3("scales", i, actual.scales[i],
            expected.scales[i], tolerances.scale);
        if (!QuaternionsEquivalent(actual.rotations[i],
                expected.rotations[i], tolerances.rotation)) {
            fail("rotations[" + std::to_string(i) +
                "] differs beyond tolerance under either sign");
        }
        if (!close(actual.opacities[i], expected.opacities[i],
                tolerances.opacity)) {
            fail("opacities[" + std::to_string(i) + "] " +
                std::to_string(actual.opacities[i]) + " != expected " +
                std::to_string(expected.opacities[i]));
        }
        checkFloat3("dcCoefficients", i, actual.dcCoefficients[i],
            expected.dcCoefficients[i], tolerances.shCoefficient);
    }

    const std::size_t restPerGaussian =
        expected.gaussianCount == 0
            ? 0
            : expected.restCoefficients.size() / expected.gaussianCount;
    for (std::size_t i = 0; i < actual.restCoefficients.size(); ++i) {
        const std::size_t gaussian =
            restPerGaussian == 0 ? 0 : i / restPerGaussian;
        const std::size_t coefficient =
            restPerGaussian == 0 ? 0 : i % restPerGaussian;
        const Float3& got = actual.restCoefficients[i];
        const Float3& want = expected.restCoefficients[i];
        if (!close(got.x, want.x, tolerances.shCoefficient) ||
            !close(got.y, want.y, tolerances.shCoefficient) ||
            !close(got.z, want.z, tolerances.shCoefficient)) {
            fail("restCoefficients[gaussian " + std::to_string(gaussian) +
                ", coefficient " + std::to_string(coefficient) + "] (" +
                std::to_string(got.x) + ", " + std::to_string(got.y) + ", " +
                std::to_string(got.z) + ") != expected (" +
                std::to_string(want.x) + ", " + std::to_string(want.y) +
                ", " + std::to_string(want.z) + ")");
        }
    }

    // The derived extent, through the same ComputeCloudExtent the writer
    // authors: positions or scales that individually pass tolerance cannot
    // hide an extent disagreement, and the expected authored extent is known
    // without a stage.
    Float3 actualMinimum, actualMaximum, expectedMinimum, expectedMaximum;
    const bool actualHasExtent = ComputeCloudExtent(
        actual.positions.data(), actual.scales.data(), actual.gaussianCount,
        &actualMinimum, &actualMaximum);
    const bool expectedHasExtent = ComputeCloudExtent(
        expected.positions.data(), expected.scales.data(),
        expected.gaussianCount, &expectedMinimum, &expectedMaximum);
    if (actualHasExtent != expectedHasExtent) {
        fail("extent computability differs from expected");
    } else if (actualHasExtent) {
        const auto extentClose = [&](const Float3& a, const Float3& b) {
            return close(a.x, b.x, tolerances.extent) &&
                close(a.y, b.y, tolerances.extent) &&
                close(a.z, b.z, tolerances.extent);
        };
        if (!extentClose(actualMinimum, expectedMinimum) ||
            !extentClose(actualMaximum, expectedMaximum)) {
            fail("derived extent differs beyond tolerance");
        }
    }

    return mismatches;
}

// --- Invalid shared-model cases ---------------------------------------------

struct InvalidCloudCase {
    // Stable case name for test output ("negative-scale", "opacity-logit"...).
    std::string name;
    GaussianCloudData cloud;
};

// One cloud per contract rule, each violating exactly that rule relative to
// the canonical one-Gaussian cloud. Every case must fail
// ValidateGaussianCloud; a decoder or validator change that lets one through
// is a contract regression. Bundles run their own *file-level* malformed
// fixtures on top of these -- these are model-level, after parsing succeeded.
inline std::vector<InvalidCloudCase> MakeInvalidCloudCases()
{
    std::vector<InvalidCloudCase> cases;
    const auto add = [&cases](const char* name, GaussianCloudData cloud) {
        cases.push_back({name, std::move(cloud)});
    };
    const GaussianCloudData base = MakeCanonicalOneGaussianCloud();

    {
        GaussianCloudData cloud;
        add("empty-cloud", std::move(cloud));
    }
    {
        GaussianCloudData cloud = base;
        cloud.positions.clear();
        add("length-mismatch", std::move(cloud));
    }
    {
        GaussianCloudData cloud = base;
        cloud.positions[0].y = std::numeric_limits<float>::quiet_NaN();
        add("non-finite-position", std::move(cloud));
    }
    {
        GaussianCloudData cloud = base;
        cloud.scales[0].z = 0.0f;
        add("zero-scale", std::move(cloud));
    }
    {
        // A log-encoded scale reaching the model shows up negative.
        GaussianCloudData cloud = base;
        cloud.scales[0] = {-4.6f, -4.6f, -4.6f};
        add("log-scale", std::move(cloud));
    }
    {
        // An opacity logit reaching the model is outside [0, 1].
        GaussianCloudData cloud = base;
        cloud.opacities[0] = 6.9f;
        add("opacity-logit", std::move(cloud));
    }
    {
        GaussianCloudData cloud = base;
        cloud.rotations[0] = {2.0f, 0.0f, 0.0f, 0.0f};
        add("unnormalized-quaternion", std::move(cloud));
    }
    {
        GaussianCloudData cloud = base;
        cloud.rotations[0].i = std::numeric_limits<float>::infinity();
        add("non-finite-quaternion", std::move(cloud));
    }
    {
        // Rest sized for degree 1 while the cloud declares degree 0.
        GaussianCloudData cloud = base;
        cloud.restCoefficients.resize(3);
        add("rest-length-mismatch", std::move(cloud));
    }
    {
        // Degree above the supported ceiling, rest sized consistently for it.
        GaussianCloudData cloud = base;
        cloud.shDegree = kMaxShDegree + 1;
        cloud.restCoefficients.resize(
            cloud.CoefficientsPerGaussian() - 1);
        add("unsupported-sh-degree", std::move(cloud));
    }
    {
        GaussianCloudData cloud = base;
        cloud.shDegree = 1;
        cloud.restCoefficients.resize(3);
        cloud.restCoefficients[1].x = std::numeric_limits<float>::infinity();
        add("non-finite-rest-coefficient", std::move(cloud));
    }
    return cases;
}

} // namespace openstrata::gs::testing
