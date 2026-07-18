// SPDX-License-Identifier: Apache-2.0
#include "openstrata/gs/GaussianMath.h"

#include <cmath>
#include <iostream>
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

} // namespace

int main()
{
    TestTransforms();
    TestShLayout();
    TestValidation();
    return failures == 0 ? 0 : 1;
}
