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
    TestCloudContractChecker();
    return failures == 0 ? 0 : 1;
}
