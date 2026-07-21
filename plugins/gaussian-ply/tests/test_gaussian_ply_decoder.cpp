// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianPlyDecoder.h"
#include "io/GaussianPlyImportOptions.h"
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/testing/CloudContract.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

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

bool CloseF3(const gs::Float3& value, float x, float y, float z)
{
    return Close(value.x, x) && Close(value.y, y) && Close(value.z, z);
}

bool HasCode(const std::string& error, const char* code)
{
    return error.rfind(std::string("[") + code + "]", 0) == 0;
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
    CHECK(CloseF3(cloud.restCoefficients[0], 1.0f, 4.0f, 7.0f));
    CHECK(CloseF3(cloud.restCoefficients[2], 3.0f, 6.0f, 9.0f));
}

// Exact coefficient assertions for every supported non-zero degree. The
// channel-major source values are c+1 (red), 101+c (green), 201+c (blue) for
// per-channel coefficient index c, so the interleaved result is fully
// predictable at both degrees.
void TestHighDegreeShLayout(const char* fixture, int degree)
{
    gs::ply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(Fixture(fixture), &cloud, nullptr, &error));
    CHECK(cloud.shDegree == degree);
    const std::size_t restCount =
        static_cast<std::size_t>((degree + 1) * (degree + 1) - 1);
    CHECK(cloud.restCoefficients.size() == restCount);
    for (std::size_t c = 0; c < restCount; ++c) {
        CHECK(CloseF3(cloud.restCoefficients[c],
            1.0f + c, 101.0f + c, 201.0f + c));
    }
    CHECK(CloseF3(cloud.dcCoefficients[0], 0.1f, 0.2f, 0.3f));
}

// Property resolution is name-based: a scrambled declaration order must
// decode to exactly the same cloud as the canonical degree-1 fixture.
void TestReorderedProperties()
{
    gs::ply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData canonical;
    gs::GaussianCloudData reordered;
    std::string error;
    CHECK(decoder.Decode(
        Fixture("degree-1-sh.ply"), &canonical, nullptr, &error));
    CHECK(decoder.Decode(
        Fixture("reordered-properties.ply"), &reordered, nullptr, &error));
    CHECK(canonical.gaussianCount == reordered.gaussianCount);
    CHECK(canonical.shDegree == reordered.shDegree);
    CHECK(canonical.restCoefficients.size() == reordered.restCoefficients.size());
    for (std::size_t i = 0; i < canonical.restCoefficients.size(); ++i) {
        const gs::Float3& a = canonical.restCoefficients[i];
        CHECK(CloseF3(reordered.restCoefficients[i], a.x, a.y, a.z));
    }
    CHECK(CloseF3(reordered.positions[0],
        canonical.positions[0].x,
        canonical.positions[0].y,
        canonical.positions[0].z));
    CHECK(Close(reordered.opacities[0], canonical.opacities[0]));
}

void TestThreeGaussians()
{
    gs::ply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(
        Fixture("three-gaussian-binary-le.ply"), &cloud, nullptr, &error));
    CHECK(cloud.gaussianCount == 3);
    CHECK(cloud.shDegree == 1);

    CHECK(CloseF3(cloud.positions[1], -4.0f, 5.0f, -6.0f));
    CHECK(CloseF3(cloud.positions[2], 7.0f, -8.0f, 9.0f));

    CHECK(CloseF3(cloud.scales[0],
        1.0f, std::exp(0.5f), std::exp(1.0f)));
    CHECK(CloseF3(cloud.scales[1],
        std::exp(-1.0f), 1.0f, std::exp(0.25f)));
    CHECK(CloseF3(cloud.scales[2],
        std::exp(2.0f), std::exp(-0.5f), 1.0f));

    CHECK(Close(cloud.rotations[1].real, 0.5f));
    CHECK(Close(cloud.rotations[1].i, 0.5f));
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    CHECK(Close(cloud.rotations[2].real, invSqrt2));
    CHECK(Close(cloud.rotations[2].k, invSqrt2));

    CHECK(Close(cloud.opacities[0], 0.26894143f));
    CHECK(Close(cloud.opacities[1], 0.5f));
    CHECK(Close(cloud.opacities[2], 0.8807971f));

    CHECK(cloud.restCoefficients.size() == 9);
    CHECK(CloseF3(cloud.restCoefficients[0], 1.0f, 4.0f, 7.0f));
    CHECK(CloseF3(cloud.restCoefficients[3], 11.0f, 14.0f, 17.0f));
    CHECK(CloseF3(cloud.restCoefficients[8], 23.0f, 26.0f, 29.0f));
}

void TestMetadata()
{
    gs::ply::GaussianPlyDecoder decoder;
    gs::ply::GaussianPlyMetadata metadata;
    std::string error;

    CHECK(decoder.DecodeMetadata(
        Fixture("degree-3-sh.ply"), &metadata, &error));
    CHECK(metadata.gaussianCount == 1);
    CHECK(metadata.shDegree == 3);

    CHECK(decoder.DecodeMetadata(
        Fixture("three-gaussian-binary-le.ply"), &metadata, &error));
    CHECK(metadata.gaussianCount == 3);
    CHECK(metadata.shDegree == 1);

    // Header validation applies unchanged on the metadata path.
    error.clear();
    CHECK(!decoder.DecodeMetadata(
        Fixture("duplicate-sh-index.ply"), &metadata, &error));
    CHECK(HasCode(error, "GSPLY-E006"));
}

void TestFailures()
{
    gs::ply::GaussianPlyDecoder decoder;

    const std::map<std::string, const char*> expectedCodes = {
        {"mesh-not-gaussian.ply", "GSPLY-E001"},
        {"empty-vertex.ply", "GSPLY-E002"},
        {"missing-opacity.ply", "GSPLY-E003"},
        {"list-property.ply", "GSPLY-E004"},
        {"invalid-sh-name.ply", "GSPLY-E005"},
        {"duplicate-sh-index.ply", "GSPLY-E006"},
        {"non-contiguous-sh.ply", "GSPLY-E007"},
        {"sh-count-not-rgb.ply", "GSPLY-E008"},
        {"sh-invalid-degree.ply", "GSPLY-E009"},
        {"degree-4-sh.ply", "GSPLY-E017"},
        {"nan-opacity-binary-le.ply", "GSPLY-E010"},
        {"out-of-range-double.ply", "GSPLY-E010"},
        {"overflow-scale.ply", "GSPLY-E012"},
    };
    for (const auto& [fixture, code] : expectedCodes) {
        gs::GaussianCloudData cloud;
        std::string error;
        CHECK(!decoder.Decode(Fixture(fixture.c_str()), &cloud, nullptr, &error));
        if (!HasCode(error, code)) {
            std::cerr << fixture << ": expected " << code
                      << ", got: " << error << "\n";
            ++failures;
        }
    }

    CHECK(!decoder.CanRead(Fixture("mesh-not-gaussian.ply")));

    // Substring stability for the most user-facing messages.
    {
        gs::GaussianCloudData cloud;
        std::string error;
        CHECK(!decoder.Decode(
            Fixture("mesh-not-gaussian.ply"), &cloud, nullptr, &error));
        CHECK(error.find("not a supported Gaussian") != std::string::npos);
        error.clear();
        CHECK(!decoder.Decode(
            Fixture("missing-opacity.ply"), &cloud, nullptr, &error));
        CHECK(error.find("opacity") != std::string::npos);
        error.clear();
        // A truncated payload fails inside the container or column pass; the
        // exact stage is an implementation detail but the code family is not.
        CHECK(!decoder.Decode(
            Fixture("truncated-binary-le.ply"), &cloud, nullptr, &error));
        CHECK(error.rfind("[GSPLY-E", 0) == 0);
        error.clear();
        CHECK(!decoder.Decode(
            Fixture("out-of-range-double.ply"), &cloud, nullptr, &error));
        CHECK(error.find("non-finite or out-of-range") != std::string::npos);
    }
}

void TestImportOptionParsing()
{
    gs::ply::GaussianPlyImportOptions options;
    std::string error;

    CHECK(gs::ply::ParseImportOptions({}, &options, &error));
    CHECK(options.shDegree == -1);
    CHECK(options.opacityThreshold < 0.0f);
    CHECK(Close(options.scaleMultiplier, 1.0f));

    CHECK(gs::ply::ParseImportOptions(
        {{"shDegree", "2"}, {"opacityThreshold", "0.25"},
         {"scaleMultiplier", "1.5"}, {"unrelatedHostArg", "x"}},
        &options, &error));
    CHECK(options.shDegree == 2);
    CHECK(Close(options.opacityThreshold, 0.25f));
    CHECK(Close(options.scaleMultiplier, 1.5f));

    const std::map<std::string, std::string> badValues[] = {
        {{"shDegree", "4"}},
        {{"shDegree", "-1"}},
        {{"shDegree", "abc"}},
        {{"shDegree", "1.5"}},
        {{"opacityThreshold", "1.5"}},
        {{"opacityThreshold", "-0.1"}},
        {{"opacityThreshold", "nope"}},
        {{"scaleMultiplier", "0"}},
        {{"scaleMultiplier", "-2"}},
        {{"scaleMultiplier", "inf"}},
    };
    for (const auto& arguments : badValues) {
        error.clear();
        if (gs::ply::ParseImportOptions(arguments, &options, &error) ||
            !HasCode(error, "GSPLY-E201")) {
            std::cerr << "expected GSPLY-E201 for '"
                      << arguments.begin()->first << "' = '"
                      << arguments.begin()->second << "', got: "
                      << error << "\n";
            ++failures;
        }
    }
}

void TestImportOptionApplication()
{
    gs::ply::GaussianPlyDecoder decoder;
    std::string error;

    {
        gs::GaussianCloudData cloud;
        CHECK(decoder.Decode(
            Fixture("three-gaussian-binary-le.ply"), &cloud, nullptr, &error));
        gs::ply::GaussianPlyImportOptions options;
        options.scaleMultiplier = 2.0f;
        CHECK(gs::ply::ApplyImportOptions(options, &cloud, &error));
        CHECK(CloseF3(cloud.scales[0],
            2.0f, 2.0f * std::exp(0.5f), 2.0f * std::exp(1.0f)));
    }

    {
        gs::GaussianCloudData cloud;
        CHECK(decoder.Decode(
            Fixture("three-gaussian-binary-le.ply"), &cloud, nullptr, &error));
        gs::ply::GaussianPlyImportOptions options;
        options.opacityThreshold = 0.4f;
        CHECK(gs::ply::ApplyImportOptions(options, &cloud, &error));
        // sigmoid(-1) ~= 0.269 drops; sigmoid(0) = 0.5 and sigmoid(2) remain.
        CHECK(cloud.gaussianCount == 2);
        CHECK(CloseF3(cloud.positions[0], -4.0f, 5.0f, -6.0f));
        CHECK(CloseF3(cloud.positions[1], 7.0f, -8.0f, 9.0f));
        CHECK(cloud.restCoefficients.size() == 6);
        CHECK(CloseF3(cloud.restCoefficients[0], 11.0f, 14.0f, 17.0f));
        CHECK(CloseF3(cloud.restCoefficients[5], 23.0f, 26.0f, 29.0f));
    }

    {
        gs::GaussianCloudData cloud;
        CHECK(decoder.Decode(
            Fixture("three-gaussian-binary-le.ply"), &cloud, nullptr, &error));
        gs::ply::GaussianPlyImportOptions options;
        options.shDegree = 0;
        CHECK(gs::ply::ApplyImportOptions(options, &cloud, &error));
        CHECK(cloud.shDegree == 0);
        CHECK(cloud.restCoefficients.empty());
        CHECK(cloud.gaussianCount == 3);
    }

    {
        // Requesting a higher degree than the source never upsamples.
        gs::GaussianCloudData cloud;
        CHECK(decoder.Decode(
            Fixture("three-gaussian-binary-le.ply"), &cloud, nullptr, &error));
        gs::ply::GaussianPlyImportOptions options;
        options.shDegree = 3;
        CHECK(gs::ply::ApplyImportOptions(options, &cloud, &error));
        CHECK(cloud.shDegree == 1);
        CHECK(cloud.restCoefficients.size() == 9);
    }

    {
        // Capping to an intermediate degree with several Gaussians exercises
        // the in-place forward compaction: every Gaussian past the first has
        // its kept coefficients moved down from i * oldRest to i * newRest.
        // A synthetic degree-2 cloud keeps the expected values obvious.
        gs::GaussianCloudData cloud;
        cloud.gaussianCount = 2;
        cloud.shDegree = 2;
        const std::size_t oldRest = cloud.CoefficientsPerGaussian() - 1;
        cloud.restCoefficients.resize(cloud.gaussianCount * oldRest);
        for (std::size_t i = 0; i < cloud.gaussianCount; ++i) {
            for (std::size_t c = 0; c < oldRest; ++c) {
                const float base = 100.0f * i + c;
                cloud.restCoefficients[i * oldRest + c] =
                    {base, base + 0.25f, base + 0.5f};
            }
        }
        gs::ply::GaussianPlyImportOptions options;
        options.shDegree = 1;
        CHECK(gs::ply::ApplyImportOptions(options, &cloud, &error));
        CHECK(cloud.shDegree == 1);
        CHECK(cloud.restCoefficients.size() == 6);
        for (std::size_t i = 0; i < 2; ++i) {
            for (std::size_t c = 0; c < 3; ++c) {
                const float base = 100.0f * i + c;
                CHECK(CloseF3(cloud.restCoefficients[i * 3 + c],
                    base, base + 0.25f, base + 0.5f));
            }
        }
    }

    {
        gs::GaussianCloudData cloud;
        CHECK(decoder.Decode(
            Fixture("three-gaussian-binary-le.ply"), &cloud, nullptr, &error));
        gs::ply::GaussianPlyImportOptions options;
        options.opacityThreshold = 0.95f;
        error.clear();
        CHECK(!gs::ply::ApplyImportOptions(options, &cloud, &error));
        CHECK(HasCode(error, "GSPLY-E202"));
    }
}

// Holds the PLY decoder to the format-independent invariants of
// GAUSSIAN_MODEL_CONTRACT.md. The checker itself lives in gaussianCore, so
// gaussian-spz runs this same code against its own fixtures rather than a copy
// that can drift from it.
//
// SH *ordering* is not covered here — it needs known expected values, which is
// what TestShLayout below provides.
void TestContractConformance()
{
    // Every fixture the bundle considers valid, across encodings, SH degrees,
    // property orderings, and Gaussian counts.
    const char* kValidFixtures[] = {
        "one-gaussian-ascii.ply",
        "one-gaussian-binary-le.ply",
        "degree-1-sh.ply",
        "degree-2-sh.ply",
        "degree-3-sh.ply",
        "reordered-properties.ply",
        "three-gaussian-binary-le.ply",
    };

    for (const char* fixture : kValidFixtures) {
        gs::GaussianCloudData cloud;
        std::vector<std::string> warnings;
        std::string error;
        gs::ply::GaussianPlyDecoder decoder;
        if (!decoder.Decode(Fixture(fixture), &cloud, &warnings, &error)) {
            std::cerr << fixture << ": decode failed: " << error << '\n';
            ++failures;
            continue;
        }
        for (const std::string& violation :
                gs::testing::CheckCloudContract(cloud)) {
            std::cerr << fixture << ": " << violation << '\n';
            ++failures;
        }
    }
}

} // namespace

int main()
{
    TestContractConformance();
    TestDegreeZero("one-gaussian-ascii.ply");
    TestDegreeZero("one-gaussian-binary-le.ply");
    TestShLayout();
    TestHighDegreeShLayout("degree-2-sh.ply", 2);
    TestHighDegreeShLayout("degree-3-sh.ply", 3);
    TestReorderedProperties();
    TestThreeGaussians();
    TestMetadata();
    TestFailures();
    TestImportOptionParsing();
    TestImportOptionApplication();
    return failures == 0 ? 0 : 1;
}
