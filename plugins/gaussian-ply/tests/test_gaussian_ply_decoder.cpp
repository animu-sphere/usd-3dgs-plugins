// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianPlyDecoder.h"

#include <cmath>
#include <filesystem>
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

std::string Fixture(const char* name)
{
    return (std::filesystem::path(GAUSSIAN_PLY_FIXTURE_DIR) / name).string();
}

bool Close(float a, float b, float epsilon = 1.0e-5f)
{
    return std::abs(a - b) <= epsilon;
}

void TestDegreeZero(const char* fixture)
{
    gs::ply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    const std::string path = Fixture(fixture);
    CHECK(decoder.CanRead(path));
    CHECK(decoder.Decode(path, &cloud, &warnings, &error));
    CHECK(error.empty());
    CHECK(cloud.gaussianCount == 1);
    CHECK(cloud.shDegree == 0);
    CHECK(cloud.positions.size() == 1);
    CHECK(Close(cloud.positions[0].x, 1.0f));
    CHECK(Close(cloud.positions[0].y, 2.0f));
    CHECK(Close(cloud.positions[0].z, 3.0f));
    CHECK(Close(cloud.scales[0].x, 1.0f));
    CHECK(Close(cloud.scales[0].y, 2.0f));
    CHECK(Close(cloud.scales[0].z, 0.5f));
    CHECK(Close(cloud.rotations[0].real, 1.0f));
    CHECK(Close(cloud.opacities[0], 0.5f));
    CHECK(Close(cloud.dcCoefficients[0].x, 0.1f));
    CHECK(cloud.restCoefficients.empty());
}

void TestShLayout()
{
    gs::ply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(Fixture("degree-1-sh.ply"), &cloud, nullptr, &error));
    CHECK(cloud.shDegree == 1);
    CHECK(cloud.restCoefficients.size() == 3);
    CHECK(Close(cloud.restCoefficients[0].x, 1.0f));
    CHECK(Close(cloud.restCoefficients[0].y, 4.0f));
    CHECK(Close(cloud.restCoefficients[0].z, 7.0f));
    CHECK(Close(cloud.restCoefficients[2].x, 3.0f));
    CHECK(Close(cloud.restCoefficients[2].y, 6.0f));
    CHECK(Close(cloud.restCoefficients[2].z, 9.0f));
}

void TestFailures()
{
    gs::ply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;

    CHECK(!decoder.CanRead(Fixture("mesh-not-gaussian.ply")));
    CHECK(!decoder.Decode(Fixture("mesh-not-gaussian.ply"), &cloud, nullptr, &error));
    CHECK(error.find("not a supported Gaussian") != std::string::npos);

    error.clear();
    CHECK(!decoder.Decode(Fixture("missing-opacity.ply"), &cloud, nullptr, &error));
    CHECK(error.find("opacity") != std::string::npos);

    error.clear();
    CHECK(!decoder.Decode(Fixture("truncated-binary-le.ply"), &cloud, nullptr, &error));
    CHECK(!error.empty());
}

} // namespace

int main()
{
    TestDegreeZero("one-gaussian-ascii.ply");
    TestDegreeZero("one-gaussian-binary-le.ply");
    TestShLayout();
    TestFailures();
    return failures == 0 ? 0 : 1;
}
