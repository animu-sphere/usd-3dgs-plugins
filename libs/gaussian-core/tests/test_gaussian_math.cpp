// SPDX-License-Identifier: Apache-2.0
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/testing/CloudContract.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace gs = openstrata::gs;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

bool Close(float a, float b, float epsilon = 1.0e-6f)
{
    return std::abs(a - b) <= epsilon;
}

bool CloseF3(const gs::Float3& a, const gs::Float3& b)
{
    return Close(a.x, b.x) && Close(a.y, b.y) && Close(a.z, b.z);
}

void TestTransforms()
{
    CHECK(Close(gs::Sigmoid(0.0f), 0.5f));
    CHECK(gs::Sigmoid(1000.0f) == 1.0f);
    CHECK(gs::Sigmoid(-1000.0f) == 0.0f);

    gs::Float3 scale;
    CHECK(gs::DecodeLogScale({0.0f, std::log(2.0f), std::log(0.5f)}, &scale));
    CHECK(Close(scale.x, 1.0f));
    CHECK(Close(scale.y, 2.0f));
    CHECK(Close(scale.z, 0.5f));

    gs::Quaternion q;
    bool identity = false;
    bool changed = false;
    CHECK(gs::NormalizeQuaternion({2.0f, 0.0f, 0.0f, 0.0f},
                                  &q, &identity, &changed));
    CHECK(!identity && changed && Close(q.real, 1.0f));
    CHECK(gs::NormalizeQuaternion({0.0f, 0.0f, 0.0f, 0.0f},
                                  &q, &identity, &changed));
    CHECK(identity && changed && Close(q.real, 1.0f));
}

void TestShLayout()
{
    int degree = -1;
    CHECK(gs::InferShDegree(1, &degree) && degree == 0);
    CHECK(gs::InferShDegree(4, &degree) && degree == 1);
    CHECK(gs::InferShDegree(16, &degree) && degree == 3);
    CHECK(!gs::InferShDegree(15, &degree));
}

void TestValidation()
{
    gs::GaussianCloudData cloud;
    cloud.gaussianCount = 1;
    cloud.positions.push_back({1.0f, 2.0f, 3.0f});
    cloud.scales.push_back({1.0f, 1.0f, 1.0f});
    cloud.rotations.push_back({});
    cloud.opacities.push_back(0.5f);
    cloud.dcCoefficients.push_back({0.1f, 0.2f, 0.3f});

    std::string error;
    CHECK(gs::ValidateGaussianCloud(cloud, &error));
    cloud.positions.clear();
    CHECK(!gs::ValidateGaussianCloud(cloud, &error));
    CHECK(!error.empty());

    const auto oneGaussian = [] {
        gs::GaussianCloudData valid;
        valid.gaussianCount = 1;
        valid.positions.push_back({1.0f, 2.0f, 3.0f});
        valid.scales.push_back({1.0f, 1.0f, 1.0f});
        valid.rotations.push_back({});
        valid.opacities.push_back(0.5f);
        valid.dcCoefficients.push_back({0.1f, 0.2f, 0.3f});
        return valid;
    };

    // GAUSSIAN_MODEL_CONTRACT.md §3: a degree above kMaxShDegree is rejected
    // even when the rest array is sized consistently for it.
    gs::GaussianCloudData degree4 = oneGaussian();
    degree4.shDegree = gs::kMaxShDegree + 1;
    degree4.restCoefficients.resize(
        degree4.CoefficientsPerGaussian() - 1);
    CHECK(!gs::ValidateGaussianCloud(degree4, &error));

    // §4: the gate rejects an unnormalized quaternion, while one within the
    // 1e-4 normalization tolerance passes.
    gs::GaussianCloudData unnormalized = oneGaussian();
    unnormalized.rotations[0] = {2.0f, 0.0f, 0.0f, 0.0f};
    CHECK(!gs::ValidateGaussianCloud(unnormalized, &error));

    gs::GaussianCloudData nearUnit = oneGaussian();
    nearUnit.rotations[0] = {1.00005f, 0.0f, 0.0f, 0.0f};
    CHECK(gs::ValidateGaussianCloud(nearUnit, &error));
}

// The RDF <-> RUB rest-coefficient sign table (ADR 0001), restated here so
// FlipYZAxes is checked against an independent copy rather than against
// itself. Derived from the reference flipSh basis
// {y, z, x, xy, yz, zz, xz, xx-yy, band 3...} at (x, y, z) = (+1, -1, -1).
constexpr float kExpectedShFlip[15] = {
    -1.0f, -1.0f, +1.0f,                       // band 1
    -1.0f, +1.0f, +1.0f, -1.0f, +1.0f,         // band 2
    -1.0f, +1.0f, -1.0f, -1.0f, +1.0f, -1.0f,  // band 3
    +1.0f,
};

void TestFlipYZAxes()
{
    gs::GaussianCloudData cloud;
    cloud.gaussianCount = 2;
    cloud.shDegree = 3;
    for (int i = 0; i < 2; ++i) {
        cloud.positions.push_back({1.0f, 2.0f, 3.0f});
        cloud.scales.push_back({0.5f, 0.6f, 0.7f});
        cloud.rotations.push_back({0.5f, 0.5f, 0.5f, 0.5f});
        cloud.opacities.push_back(0.5f);
        cloud.dcCoefficients.push_back({0.1f, 0.2f, 0.3f});
        for (int c = 0; c < 15; ++c) {
            cloud.restCoefficients.push_back(
                {1.0f + c, 101.0f + c, 201.0f + c});
        }
    }

    gs::GaussianCloudData flipped = cloud;
    gs::FlipYZAxes(&flipped);

    for (int i = 0; i < 2; ++i) {
        CHECK(CloseF3(flipped.positions[i], {1.0f, -2.0f, -3.0f}));
        // Scales and the DC term are frame-flip invariants.
        CHECK(CloseF3(flipped.scales[i], cloud.scales[i]));
        CHECK(CloseF3(flipped.dcCoefficients[i], cloud.dcCoefficients[i]));
        CHECK(Close(flipped.rotations[i].real, 0.5f));
        CHECK(Close(flipped.rotations[i].i, 0.5f));
        CHECK(Close(flipped.rotations[i].j, -0.5f));
        CHECK(Close(flipped.rotations[i].k, -0.5f));
        for (int c = 0; c < 15; ++c) {
            const gs::Float3& actual = flipped.restCoefficients[i * 15 + c];
            const gs::Float3& source = cloud.restCoefficients[i * 15 + c];
            const float flip = kExpectedShFlip[c];
            CHECK(Close(actual.x, flip * source.x));
            CHECK(Close(actual.y, flip * source.y));
            CHECK(Close(actual.z, flip * source.z));
        }
    }

    // The conversion is an involution: flipping back restores the source.
    gs::FlipYZAxes(&flipped);
    for (std::size_t i = 0; i < cloud.restCoefficients.size(); ++i) {
        CHECK(CloseF3(flipped.restCoefficients[i], cloud.restCoefficients[i]));
    }
    CHECK(CloseF3(flipped.positions[0], cloud.positions[0]));
    CHECK(Close(flipped.rotations[0].j, cloud.rotations[0].j));
}

// The contract checker is what every decoder is held to, so it needs its own
// coverage: a checker that silently accepts everything would let a real
// decoder regression through while the bundle suites stayed green. Each case
// perturbs one rule of GAUSSIAN_MODEL_CONTRACT.md §3 from a conforming cloud.
void TestCloudContractChecker()
{
    const auto conforming = [] {
        gs::GaussianCloudData cloud;
        cloud.gaussianCount = 2;
        cloud.shDegree = 1;
        for (int i = 0; i < 2; ++i) {
            cloud.positions.push_back({1.0f, 2.0f, 3.0f});
            cloud.scales.push_back({0.5f, 0.5f, 0.5f});
            cloud.rotations.push_back({});
            cloud.opacities.push_back(0.5f);
            cloud.dcCoefficients.push_back({0.1f, 0.2f, 0.3f});
            // (1+1)^2 - 1 = 3 rest triples per Gaussian.
            for (int j = 0; j < 3; ++j) {
                cloud.restCoefficients.push_back({0.0f, 0.0f, 0.0f});
            }
        }
        return cloud;
    };

    CHECK(gs::testing::CheckCloudContract(conforming()).empty());

    // A log-scale reaching the model shows up as a non-positive scale.
    gs::GaussianCloudData logScale = conforming();
    logScale.scales[1] = {-1.6f, -1.6f, -1.6f};
    CHECK(!gs::testing::CheckCloudContract(logScale).empty());

    // An opacity logit is outside [0, 1].
    gs::GaussianCloudData logit = conforming();
    logit.opacities[0] = 4.2f;
    CHECK(!gs::testing::CheckCloudContract(logit).empty());

    // An unnormalized quaternion.
    gs::GaussianCloudData unnormalized = conforming();
    unnormalized.rotations[0] = {0.5f, 0.5f, 0.5f, 0.5f};
    unnormalized.rotations[0].real = 2.0f;
    CHECK(!gs::testing::CheckCloudContract(unnormalized).empty());

    // Rest coefficients sized for the wrong SH degree.
    gs::GaussianCloudData shortRest = conforming();
    shortRest.restCoefficients.pop_back();
    CHECK(!gs::testing::CheckCloudContract(shortRest).empty());

    // A non-finite position.
    gs::GaussianCloudData infinite = conforming();
    infinite.positions[1].y = std::numeric_limits<float>::infinity();
    CHECK(!gs::testing::CheckCloudContract(infinite).empty());
}

} // namespace

int main()
{
    TestTransforms();
    TestShLayout();
    TestValidation();
    TestFlipYZAxes();
    TestCloudContractChecker();
    return failures == 0 ? 0 : 1;
}
