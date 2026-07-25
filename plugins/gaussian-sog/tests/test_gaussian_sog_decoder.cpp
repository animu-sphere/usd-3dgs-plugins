// SPDX-License-Identifier: Apache-2.0
//
// Semantic assertions for GaussianSogDecoder: the decoder-test-kit round trip
// SOG_FORMAT.md §5 requires, hand-checked values for every attribute, the
// RDF->RUB frame conversion, palette-resolved higher-order SH, the metadata
// path, the import-statistics seam, and the semantic rejections.

#include "io/GaussianSogDecoder.h"
#include "io/GaussianSogDiagnostics.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"
#include "openstrata/gs/testing/CloudContract.h"
#include "openstrata/gs/testing/DecoderTestKit.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace gs = openstrata::gs;
namespace gssog = openstrata::gs::sog;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

std::string Fixture(const std::string& name)
{
    return (std::filesystem::path(GAUSSIAN_SOG_FIXTURE_DIR) / name).string();
}

bool HasCode(const std::string& error, const char* code)
{
    return error.rfind(std::string("[") + code + "]", 0) == 0;
}

void CheckClose(float actual, float expected, float tolerance, const char* what)
{
    if (!(std::fabs(actual - expected) <= tolerance)) {
        std::cerr << what << ": expected " << expected << ", got " << actual
                  << " (tolerance " << tolerance << ")\n";
        ++failures;
    }
}

void CheckContract(const gs::GaussianCloudData& cloud)
{
    for (const std::string& violation : gs::testing::CheckCloudContract(cloud)) {
        std::cerr << "contract: " << violation << "\n";
        ++failures;
    }
}

// Tolerances derived from the SOG v2 quantization equations
// (SOG_MAPPING.md §3-§6), not from observed output:
//
// * positions are a 16-bit code across the per-axis log-domain span, so the
//   log-domain error is at most span/(2*65535); the inverse-log transform
//   amplifies it by d/dn(exp(n)-1) = |p|+1, at most ~3.3 for these fixtures.
//   (1.2/131070)*3.3 is under 3.1e-5, so 1e-4 is a safe bound.
// * scales, DC, and rest coefficients pass through codebooks the fixture
//   generator fills exactly, so only the float32 round trip of exp(log(s))
//   remains -- a few ulps.
// * rotations store three components as bytes scaled by 1/sqrt(2): a stored
//   component carries at most (1/255)*(1/sqrt(2)) ~ 2.8e-3, and the
//   reconstructed largest component absorbs the accumulated error of all
//   three, which for the kit's (0.5, 0.5, 0.5, 0.5) reaches ~6e-3.
// * opacity is a byte, so at most 1/510 ~ 2.0e-3.
// * the extent is derived from positions and 3x the largest scale, so its
//   bound is the position bound plus a scale term.
gs::testing::CloudTolerances SogTolerances()
{
    gs::testing::CloudTolerances tolerances;
    tolerances.position = 1.0e-4f;
    tolerances.scale = 1.0e-6f;
    tolerances.rotation = 1.0e-2f;
    tolerances.opacity = 2.5e-3f;
    tolerances.shCoefficient = 1.0e-6f;
    tolerances.extent = 1.0e-3f;
    return tolerances;
}

// The round trip SOG_FORMAT.md §5 requires: the kit's canonical clouds,
// encoded into SOG by tools/generate_fixtures.py and decoded back. An empty
// CompareClouds pins Gaussian order, coefficient order, channel order, the
// quaternion convention, the frame, the derived extent, and every value at
// once.
void TestKitRoundTrip(const char* fixture, const gs::GaussianCloudData& expected)
{
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    if (!gssog::GaussianSogDecoder().Decode(
            Fixture(fixture), &cloud, &warnings, &error)) {
        std::cerr << fixture << ": decode failed: " << error << "\n";
        ++failures;
        return;
    }
    CHECK(warnings.empty());
    CheckContract(cloud);
    for (const std::string& mismatch :
             gs::testing::CompareClouds(cloud, expected, SogTolerances())) {
        std::cerr << fixture << ": " << mismatch << "\n";
        ++failures;
    }
}

void TestKitRoundTrips()
{
    TestKitRoundTrip("kit-one-degree0.sog",
        gs::testing::MakeCanonicalOneGaussianCloud());
    TestKitRoundTrip("kit-multi-degree3.sog",
        gs::testing::MakeCanonicalMultiGaussianCloud());
    // Both layouts and both ZIP storage methods converge on the same model.
    TestKitRoundTrip("kit-multi-degree3-deflated.sog",
        gs::testing::MakeCanonicalMultiGaussianCloud());
    TestKitRoundTrip("unbundled-kit-multi-degree3/meta.json",
        gs::testing::MakeCanonicalMultiGaussianCloud());
}

// The known source values encoded by tools/generate_fixtures.py
// (decode-degree1.sog), stated in model terms. SOG stores PLY-native RDF
// columns, so the generator negated Y and Z on the way in and the decoder must
// negate them back: these expectations are the *model* values, and a missing or
// doubled conversion changes their signs.
void TestDegree1FullPipeline()
{
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(gssog::GaussianSogDecoder().Decode(
        Fixture("decode-degree1.sog"), &cloud, &warnings, &error));
    CHECK(error.empty());
    CHECK(warnings.empty());
    CheckContract(cloud);

    CHECK(cloud.gaussianCount == 2);
    CHECK(cloud.shDegree == 1);

    // Positions: split-precision codes through the inverse-log transform.
    CheckClose(cloud.positions[0].x, 1.0f, 1e-4f, "pos0.x");
    CheckClose(cloud.positions[0].y, 2.0f, 1e-4f, "pos0.y");
    CheckClose(cloud.positions[0].z, -0.5f, 1e-4f, "pos0.z");
    CheckClose(cloud.positions[1].x, -3.0f, 1e-4f, "pos1.x");
    CheckClose(cloud.positions[1].y, 0.25f, 1e-4f, "pos1.y");
    CheckClose(cloud.positions[1].z, 4.0f, 1e-4f, "pos1.z");

    // Scales: exp of the log-domain codebook entry, strictly positive.
    CheckClose(cloud.scales[0].x, 1.0f, 1e-6f, "scale0.x");
    CheckClose(cloud.scales[0].y, std::exp(1.0f), 1e-5f, "scale0.y");
    CheckClose(cloud.scales[0].z, std::exp(-1.0f), 1e-6f, "scale0.z");
    CheckClose(cloud.scales[1].x, std::exp(0.5f), 1e-5f, "scale1.x");
    CheckClose(cloud.scales[1].y, std::exp(-0.5f), 1e-6f, "scale1.y");
    CheckClose(cloud.scales[1].z, 1.0f, 1e-6f, "scale1.z");

    // Opacity: the sh0 alpha byte over 255, already post-sigmoid.
    CheckClose(cloud.opacities[0], 0.8f, 2.5e-3f, "opacity0");
    CheckClose(cloud.opacities[1], 0.6f, 2.5e-3f, "opacity1");

    // DC: raw band-0 coefficients straight from the sh0 codebook, no color
    // transform and no frame conversion (band 0 is isotropic).
    CheckClose(cloud.dcCoefficients[0].x, 0.0f, 1e-6f, "dc0.r");
    CheckClose(cloud.dcCoefficients[0].y, 0.5f, 1e-6f, "dc0.g");
    CheckClose(cloud.dcCoefficients[0].z, -0.5f, 1e-6f, "dc0.b");
    CheckClose(cloud.dcCoefficients[1].x, 0.9f, 1e-6f, "dc1.r");
    CheckClose(cloud.dcCoefficients[1].y, -0.9f, 1e-6f, "dc1.g");
    CheckClose(cloud.dcCoefficients[1].z, 0.0f, 1e-6f, "dc1.b");

    // Rotations reach the model scalar-first, normalized, and flipped into RUB.
    CheckClose(cloud.rotations[0].real, 1.0f, 1e-2f, "rot0.w");
    CheckClose(cloud.rotations[0].i, 0.0f, 1e-2f, "rot0.x");
    CheckClose(cloud.rotations[0].j, 0.0f, 1e-2f, "rot0.y");
    CheckClose(cloud.rotations[0].k, 0.0f, 1e-2f, "rot0.z");
    CheckClose(cloud.rotations[1].real, 0.70710678f, 1e-2f, "rot1.w");
    CheckClose(cloud.rotations[1].i, 0.70710678f, 1e-2f, "rot1.x");
    CheckClose(cloud.rotations[1].j, 0.0f, 1e-2f, "rot1.y");
    CheckClose(cloud.rotations[1].k, 0.0f, 1e-2f, "rot1.z");

    // Rest SH: Gaussian-major RGB triples, palette-resolved. Band 1's three
    // coefficients carry the flip signs (-1, -1, +1), which these values pin:
    // a lost transpose or a wrong sign table changes them.
    const std::vector<gs::Float3> expectedRest = {
        {0.1f, 0.2f, 0.3f},
        {-0.1f, -0.2f, -0.3f},
        {0.4f, -0.4f, 0.5f},
        {0.6f, 0.7f, 0.8f},
        {-0.6f, -0.7f, -0.8f},
        {0.9f, -0.9f, 0.25f},
    };
    CHECK(cloud.restCoefficients.size() == expectedRest.size());
    for (std::size_t i = 0; i < expectedRest.size() &&
             i < cloud.restCoefficients.size(); ++i) {
        const std::string label = "rest[" + std::to_string(i) + "]";
        CheckClose(cloud.restCoefficients[i].x, expectedRest[i].x, 1e-6f,
            (label + ".r").c_str());
        CheckClose(cloud.restCoefficients[i].y, expectedRest[i].y, 1e-6f,
            (label + ".g").c_str());
        CheckClose(cloud.restCoefficients[i].z, expectedRest[i].z, 1e-6f,
            (label + ".b").c_str());
    }
}

// meta.json alone yields the count and degree, with no plane decoded.
void TestMetadata()
{
    gssog::GaussianSogMetadata metadata;
    std::string error;
    CHECK(gssog::GaussianSogDecoder().DecodeMetadata(
        Fixture("kit-multi-degree3.sog"), &metadata, &error));
    CHECK(error.empty());
    CHECK(metadata.gaussianCount == 3);
    CHECK(metadata.shDegree == 3);

    CHECK(gssog::GaussianSogDecoder().DecodeMetadata(
        Fixture("kit-one-degree0.sog"), &metadata, &error));
    CHECK(metadata.gaussianCount == 1);
    CHECK(metadata.shDegree == 0);

    error.clear();
    CHECK(!gssog::GaussianSogDecoder().DecodeMetadata(
        Fixture("kit-one-degree0.sog"), nullptr, &error));
    CHECK(HasCode(error, gssog::diag::kInternalError));
}

// The decoder's half of the shared import-statistics record.
void TestImportStats()
{
    gs::GaussianCloudData cloud;
    gs::GaussianImportStats stats;
    std::string error;
    CHECK(gssog::GaussianSogDecoder().Decode(
        Fixture("kit-multi-degree3.sog"), &cloud, nullptr, &error, &stats));
    CHECK(stats.sourceFormat == gssog::kSourceFormatToken);
    CHECK(stats.sourceVersion == "2");
    CHECK(stats.gaussianCount == 3);
    CHECK(stats.shDegree == 3);
    CHECK(stats.sourceBytes > 0);
    CHECK(stats.decodedBytes == gs::ComputeDecodedByteSize(cloud));
    CHECK(stats.readSeconds >= 0.0);
    CHECK(stats.decodeSeconds >= 0.0);
    // Bounds and the authoring time belong to the caller, not the decoder.
    CHECK(!stats.hasBounds);
    CHECK(stats.authorSeconds == 0.0);
}

// A label past meta.shN.count has no centroid: those Gaussians decode with
// zero higher-order coefficients and the read warns once, as the reference
// decoder's out-of-range guard does.
void TestLabelsOutOfRangeWarn()
{
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(gssog::GaussianSogDecoder().Decode(
        Fixture("labels-out-of-range.sog"), &cloud, &warnings, &error));
    CHECK(error.empty());
    CheckContract(cloud);
    CHECK(warnings.size() == 1);
    if (!warnings.empty()) {
        CHECK(HasCode(warnings[0], gssog::diag::kShLabelsOutOfRange));
    }
    // The first Gaussian still resolves its centroid; the second is zeroed.
    CHECK(cloud.restCoefficients.size() == 6);
    if (cloud.restCoefficients.size() == 6) {
        CheckClose(cloud.restCoefficients[0].x, 0.1f, 1e-6f, "rest0.r");
        CheckClose(cloud.restCoefficients[3].x, 0.0f, 1e-6f, "rest3.r");
        CheckClose(cloud.restCoefficients[4].y, 0.0f, 1e-6f, "rest4.g");
        CheckClose(cloud.restCoefficients[5].z, 0.0f, 1e-6f, "rest5.b");
    }
}

// A quaternion tag outside 252-255 has no valid reading, so the decode fails
// rather than substituting identity.
void TestSemanticRejections()
{
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(!gssog::GaussianSogDecoder().Decode(
        Fixture("bad-quat-tag.sog"), &cloud, nullptr, &error));
    CHECK(HasCode(error, gssog::diag::kMalformedRotation));

    error.clear();
    CHECK(!gssog::GaussianSogDecoder().Decode(
        Fixture("kit-one-degree0.sog"), nullptr, nullptr, &error));
    CHECK(HasCode(error, gssog::diag::kInternalError));

    // Container failures surface through the decoder unchanged.
    error.clear();
    CHECK(!gssog::GaussianSogDecoder().Decode(
        Fixture("version-3.sog"), &cloud, nullptr, &error));
    CHECK(HasCode(error, gssog::diag::kUnsupportedVersion));
}

// Routing delegates to the reader's two gates.
void TestRoutingDelegation()
{
    const gssog::GaussianSogDecoder decoder;
    CHECK(decoder.CanReadBundled(Fixture("kit-one-degree0.sog")));
    CHECK(!decoder.CanReadBundled(Fixture("not-sog.sog")));
    CHECK(decoder.CanReadUnbundled(
        Fixture("unbundled-kit-multi-degree3/meta.json")));
    CHECK(!decoder.CanReadUnbundled(Fixture("not-sog.json")));
}

} // namespace

int main()
{
    TestKitRoundTrips();
    TestDegree1FullPipeline();
    TestMetadata();
    TestImportStats();
    TestLabelsOutOfRangeWarn();
    TestSemanticRejections();
    TestRoutingDelegation();

    if (failures != 0) {
        std::cerr << failures << " SOG decoder check(s) failed\n";
        return 1;
    }
    std::cout << "SOG decoder checks passed\n";
    return 0;
}
