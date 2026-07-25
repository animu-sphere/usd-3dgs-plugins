// SPDX-License-Identifier: Apache-2.0
//
// Cross-format equivalence, PLY against SOG: the same source models the PLY/SPZ
// suite uses, encoded a third time as bundled SOG v2 archives, must decode to
// the same GaussianCloudData.
//
// This is the check that makes the frame decision impossible to get wrong
// silently. SOG stores PLY-native (Graphdeco RDF) columns, so its decoder
// applies the same shared FlipYZAxes conversion PLY applies
// (ADR 0001, SOG_MAPPING.md §5) — out of a completely different container, with
// a completely different SH layout (a coefficient-major palette rather than
// channel-major PLY columns). Together with the PLY/SPZ suite, where SPZ
// applies no conversion at all, agreement here pins the frame and the SH sign
// table from both directions.
//
// The PLY side is lossless at float32, so it is the reference and the whole
// disagreement budget belongs to SOG quantization; the tolerances below are
// derived from the SOG step sizes (EQUIVALENCE.md §2), not tuned.

#include "equivalence_common.h"

#include "io/GaussianPlyDecoder.h"
#include "io/GaussianSogDecoder.h"
#include "openstrata/gs/GaussianCloudData.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace gs = openstrata::gs;
using namespace openstrata::gs::equivalence;

namespace {

// SOG's error profile differs from SPZ's in kind, not only in size:
//
//   position   16 bits across a per-axis *log-domain* range, so half a code
//              step is span/131070 in the log domain and (|p| + 1) times that
//              in model space, because the decode applies
//              sign(n) * (exp(|n|) - 1). `positionLogHalfStep` holds the
//              log-domain half step; the fixture generator prints the span each
//              fixture declares so these constants can be re-derived.
//   scale      exp() of a codebook entry the generator fills exactly, and the
//              PLY decoder computes the same exp() of the same float32, so
//              agreement is a few ulps rather than a quantization step.
//   dc, sh     codebook entries filled exactly; likewise a few ulps. This is
//              what makes the SH comparison sharp enough to catch a single
//              wrong sign in the 15-entry band table.
//   opacity    a uint8 over [0, 1]: half a step is 1/510.
//   rotation   smallest-three in three bytes with the dropped component
//              reconstructed as sqrt(1 - sum of squares), which amplifies the
//              stored error; 8e-3 covers the fixtures' worst case (the
//              four-way-tie quaternion, whose four components are equal).
struct SogTolerance {
    const char* name;
    float positionLogHalfStep;
    float scaleRelative;
    float opacity;
    float dc;
    float sh;
    float rotation;
};

// Widest log-domain span 1.096053 => half step 8.36e-6.
constexpr SogTolerance kSogExact{
    "SOG degree-3, exact codebooks",
    8.4e-6f, 1.0e-6f, 1.0f / 510.0f, 1.0e-6f, 1.0e-6f, 8.0e-3f,
};

// Widest log-domain span 5.488174 => half step 4.19e-5. The codebook exactness
// holds off-grid too: only positions, opacity, and rotation quantize.
constexpr SogTolerance kSogOffGrid{
    "SOG degree-1, off-grid positions",
    4.2e-5f, 1.0e-6f, 1.0f / 510.0f, 1.0e-6f, 1.0e-6f, 8.0e-3f,
};

bool Decode(
    const char* plyFixture,
    const char* sogFixture,
    gs::GaussianCloudData* ply,
    gs::GaussianCloudData* sog)
{
    const gs::ply::GaussianPlyDecoder plyDecoder;
    const gs::sog::GaussianSogDecoder sogDecoder;
    std::string plyError;
    std::string sogError;
    std::vector<std::string> plyWarnings;
    std::vector<std::string> sogWarnings;

    const bool plyOk =
        plyDecoder.Decode(Fixture(plyFixture), ply, &plyWarnings, &plyError);
    const bool sogOk =
        sogDecoder.Decode(Fixture(sogFixture), sog, &sogWarnings, &sogError);

    if (!plyOk) {
        std::cerr << plyFixture << ": " << plyError << "\n";
        ++failures;
    }
    if (!sogOk) {
        std::cerr << sogFixture << ": " << sogError << "\n";
        ++failures;
    }
    // These fixtures are clean by construction; a warning means the generator
    // emitted something unintended.
    for (const std::string& warning : plyWarnings) {
        std::cerr << plyFixture << ": unexpected warning: " << warning << "\n";
        ++failures;
    }
    for (const std::string& warning : sogWarnings) {
        std::cerr << sogFixture << ": unexpected warning: " << warning << "\n";
        ++failures;
    }
    return plyOk && sogOk;
}

void CompareClouds(
    const gs::GaussianCloudData& ply,
    const gs::GaussianCloudData& sog,
    const SogTolerance& tolerance)
{
    if (!ShapesAgree(ply, sog)) {
        return; // Sizes disagree; per-element comparison would be noise.
    }

    // |dp| <= (|p| + 1) * halfStep: the inverse-log transform's derivative at
    // the decoded value times half a log-domain code step.
    const auto checkPosition = [&](
        float reference, float other, const char* what, std::size_t index) {
        const float bound =
            (std::fabs(reference) + 1.0f) * tolerance.positionLogHalfStep;
        CheckClose(reference, other, bound, what, index);
    };

    for (std::size_t i = 0; i < ply.gaussianCount; ++i) {
        checkPosition(ply.positions[i].x, sog.positions[i].x, "position.x", i);
        checkPosition(ply.positions[i].y, sog.positions[i].y, "position.y", i);
        checkPosition(ply.positions[i].z, sog.positions[i].z, "position.z", i);

        CheckRelative(ply.scales[i].x, sog.scales[i].x,
                      tolerance.scaleRelative, "scale.x", i);
        CheckRelative(ply.scales[i].y, sog.scales[i].y,
                      tolerance.scaleRelative, "scale.y", i);
        CheckRelative(ply.scales[i].z, sog.scales[i].z,
                      tolerance.scaleRelative, "scale.z", i);

        CheckClose(ply.opacities[i], sog.opacities[i],
                   tolerance.opacity, "opacity", i);

        const gs::Quaternion& a = ply.rotations[i];
        const gs::Quaternion b = AlignSign(a, sog.rotations[i]);
        CheckClose(a.real, b.real, tolerance.rotation, "rotation.real", i);
        CheckClose(a.i, b.i, tolerance.rotation, "rotation.i", i);
        CheckClose(a.j, b.j, tolerance.rotation, "rotation.j", i);
        CheckClose(a.k, b.k, tolerance.rotation, "rotation.k", i);

        CheckClose(ply.dcCoefficients[i].x, sog.dcCoefficients[i].x,
                   tolerance.dc, "dc.r", i);
        CheckClose(ply.dcCoefficients[i].y, sog.dcCoefficients[i].y,
                   tolerance.dc, "dc.g", i);
        CheckClose(ply.dcCoefficients[i].z, sog.dcCoefficients[i].z,
                   tolerance.dc, "dc.b", i);
    }

    // Flat, Gaussian-major comparison: a palette resolved to the wrong
    // centroid, a coefficient stride error, or a channel swap all show up here
    // as value mismatches rather than as an array-length difference.
    for (std::size_t n = 0; n < ply.restCoefficients.size(); ++n) {
        CheckClose(ply.restCoefficients[n].x, sog.restCoefficients[n].x,
                   tolerance.sh, "rest.r", n);
        CheckClose(ply.restCoefficients[n].y, sog.restCoefficients[n].y,
                   tolerance.sh, "rest.g", n);
        CheckClose(ply.restCoefficients[n].z, sog.restCoefficients[n].z,
                   tolerance.sh, "rest.b", n);
    }
}

void TestPair(
    const char* label,
    const char* plyFixture,
    const char* sogFixture,
    const SogTolerance& tolerance,
    std::size_t expectedCount,
    int expectedDegree)
{
    const int before = failures;

    gs::GaussianCloudData ply;
    gs::GaussianCloudData sog;
    if (!Decode(plyFixture, sogFixture, &ply, &sog)) {
        return;
    }
    CheckContract(ply, plyFixture);
    CheckContract(sog, sogFixture);

    // Pin the shape independently of the comparison, so a pair that agrees
    // because both sides decoded nothing still fails.
    CHECK(sog.gaussianCount == expectedCount);
    CHECK(sog.shDegree == expectedDegree);
    CHECK(!sog.restCoefficients.empty());

    CompareClouds(ply, sog, tolerance);

    if (failures == before) {
        std::cout << "  ok  " << label << " (" << tolerance.name << ")\n";
    }
}

} // namespace

int main()
{
    // Degree 3: every rest coefficient is present, so all 15 sign flips are
    // compared against an independent implementation of the same table.
    TestPair(
        "degree-3 exact, SOG v2 (bundled)",
        "equiv-degree3-exact-binary-le.ply",
        "equiv-degree3-exact.sog",
        kSogExact, 4, 3);

    // Arbitrary values between quantization points, including a position three
    // orders of magnitude larger than the rest — which is where the log-domain
    // position bound has to be relative rather than absolute.
    TestPair(
        "degree-1 off-grid, SOG v2 (bundled)",
        "equiv-degree1-offgrid-binary-le.ply",
        "equiv-degree1-offgrid.sog",
        kSogOffGrid, 3, 1);

    return Report("PLY/SOG equivalence");
}
