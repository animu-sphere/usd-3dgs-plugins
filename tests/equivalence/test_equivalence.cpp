// SPDX-License-Identifier: Apache-2.0
//
// Cross-format equivalence: the PLY and SPZ decoders, given two encodings of
// one source model, must produce the same GaussianCloudData.
//
// tools/generate_equivalence_fixtures.py defines Gaussians in shared-model
// space and encodes each one twice — into a Graphdeco binary PLY and into an
// SPZ container. This test decodes both members of a pair through the two
// independent decoders and compares every attribute the model carries.
//
// The PLY encoding is lossless at float32, so the whole disagreement budget
// belongs to SPZ quantization. Tolerances are therefore derived from the SPZ
// step sizes rather than tuned until the test passed; the derivation is in
// docs/reference/EQUIVALENCE.md and mirrored in kEnvelope below.
//
// This test is the reason the two decoders cannot drift apart silently: the
// RUB->RDF flips, the SH sign table, and the two SH memory layouts (PLY
// channel-major, SPZ Gaussian-major) are each asserted against an independent
// implementation rather than against a re-derived formula.

#include "io/GaussianPlyDecoder.h"
#include "io/GaussianSpzDecoder.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/testing/CloudContract.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace gs = openstrata::gs;

namespace {

int failures = 0;

#define CHECK(expr)                                                      \
    do {                                                                 \
        if (!(expr)) {                                                   \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
            ++failures;                                                  \
        }                                                                \
    } while (false)

std::string Fixture(const char* name)
{
    return (std::filesystem::path(EQUIVALENCE_FIXTURE_DIR) / name).string();
}

// Per-attribute agreement bounds. `scale` is relative because SPZ quantizes
// the *logarithm* of the scale, so its absolute error grows with the value.
struct Tolerance {
    const char* name;
    float position;
    float scaleRelative;
    float opacity;
    float dc;
    float sh;
    float rotation;
};

// Half of one SPZ quantization step per attribute — the largest disagreement
// a correct pair of decoders can produce. Derived in EQUIVALENCE.md §2:
//
//   position       2^-13            (24-bit fixed point, fractionalBits = 12)
//   scale          exp(1/32) - 1    (log step 1/16, relative)
//   opacity        1/510            (uint8 over [0, 1])
//   dc             1/76.5           (uint8 over +/- 1/0.15 around 0.5)
//   sh             1/256            (uint8, (b - 128) / 128)
//   rotation       see below
//
// Rotation gets more than a half step because the omitted component is
// reconstructed as sqrt(1 - sum of squares): quantization error in the stored
// components propagates into it amplified by 1/(2w). The fixtures keep w well
// away from zero, which bounds the amplification; 8e-3 covers first-three
// (step 1/127.5) with margin.
constexpr Tolerance kEnvelope{
    "quantization envelope",
    1.0f / 8192.0f,     // 1.22e-4
    0.0318f,            // exp(1/32) - 1
    1.0f / 510.0f,      // 1.96e-3
    1.0f / 76.5f,       // 1.31e-2
    1.0f / 256.0f,      // 3.91e-3
    8.0e-3f,
};

// For fixtures whose source values sit exactly on SPZ quantization points,
// quantization contributes nothing and only float32 rounding remains. Holding
// those pairs to the envelope above would let a real structural error hide
// inside a tolerance three orders of magnitude too loose, so they are checked
// far more tightly. Rotation keeps the envelope value: `first-three` stores w
// implicitly, so no quaternion round-trips exactly.
constexpr Tolerance kExact{
    "exact (on-grid)",
    1.0e-5f, 1.0e-5f, 1.0e-5f, 1.0e-4f, 1.0e-5f, 8.0e-3f,
};

void CheckClose(
    float ply, float spz, float tolerance, const char* what, std::size_t index)
{
    if (!(std::fabs(ply - spz) <= tolerance)) {
        std::cerr << what << '[' << index << "]: ply " << ply << " vs spz "
                  << spz << " (delta " << std::fabs(ply - spz)
                  << " > tolerance " << tolerance << ")\n";
        ++failures;
    }
}

void CheckRelative(
    float ply, float spz, float tolerance, const char* what, std::size_t index)
{
    const float bound = tolerance * std::fabs(ply);
    if (!(std::fabs(ply - spz) <= bound)) {
        std::cerr << what << '[' << index << "]: ply " << ply << " vs spz "
                  << spz << " (relative "
                  << std::fabs(ply - spz) / std::fabs(ply) << " > " << tolerance
                  << ")\n";
        ++failures;
    }
}

void CheckContract(const gs::GaussianCloudData& cloud, const char* which)
{
    for (const std::string& violation : gs::testing::CheckCloudContract(cloud)) {
        std::cerr << which << " contract: " << violation << "\n";
        ++failures;
    }
}

bool Decode(
    const char* plyFixture,
    const char* spzFixture,
    gs::GaussianCloudData* ply,
    gs::GaussianCloudData* spz)
{
    const gs::ply::GaussianPlyDecoder plyDecoder;
    const gs::spz::GaussianSpzDecoder spzDecoder;
    std::string plyError;
    std::string spzError;
    std::vector<std::string> plyWarnings;
    std::vector<std::string> spzWarnings;

    const bool plyOk =
        plyDecoder.Decode(Fixture(plyFixture), ply, &plyWarnings, &plyError);
    const bool spzOk =
        spzDecoder.Decode(Fixture(spzFixture), spz, &spzWarnings, &spzError);

    if (!plyOk) {
        std::cerr << plyFixture << ": " << plyError << "\n";
        ++failures;
    }
    if (!spzOk) {
        std::cerr << spzFixture << ": " << spzError << "\n";
        ++failures;
    }
    // These fixtures are clean by construction; a warning means the generator
    // emitted something unintended.
    for (const std::string& warning : plyWarnings) {
        std::cerr << plyFixture << ": unexpected warning: " << warning << "\n";
        ++failures;
    }
    for (const std::string& warning : spzWarnings) {
        std::cerr << spzFixture << ": unexpected warning: " << warning << "\n";
        ++failures;
    }
    return plyOk && spzOk;
}

void CompareClouds(
    const gs::GaussianCloudData& ply,
    const gs::GaussianCloudData& spz,
    const Tolerance& tolerance)
{
    const int beforeShape = failures;
    CHECK(ply.gaussianCount == spz.gaussianCount);
    CHECK(ply.shDegree == spz.shDegree);
    CHECK(ply.positions.size() == spz.positions.size());
    CHECK(ply.scales.size() == spz.scales.size());
    CHECK(ply.rotations.size() == spz.rotations.size());
    CHECK(ply.opacities.size() == spz.opacities.size());
    CHECK(ply.dcCoefficients.size() == spz.dcCoefficients.size());
    CHECK(ply.restCoefficients.size() == spz.restCoefficients.size());
    if (failures != beforeShape) {
        return; // Sizes disagree; per-element comparison would be noise.
    }

    for (std::size_t i = 0; i < ply.gaussianCount; ++i) {
        CheckClose(ply.positions[i].x, spz.positions[i].x,
                   tolerance.position, "position.x", i);
        CheckClose(ply.positions[i].y, spz.positions[i].y,
                   tolerance.position, "position.y", i);
        CheckClose(ply.positions[i].z, spz.positions[i].z,
                   tolerance.position, "position.z", i);

        CheckRelative(ply.scales[i].x, spz.scales[i].x,
                      tolerance.scaleRelative, "scale.x", i);
        CheckRelative(ply.scales[i].y, spz.scales[i].y,
                      tolerance.scaleRelative, "scale.y", i);
        CheckRelative(ply.scales[i].z, spz.scales[i].z,
                      tolerance.scaleRelative, "scale.z", i);

        CheckClose(ply.opacities[i], spz.opacities[i],
                   tolerance.opacity, "opacity", i);

        // q and -q are the same rotation, and SPZ's `first-three` encoding
        // forces w >= 0 while PLY stores whatever sign the source used. Align
        // the signs before comparing so a legitimate double-cover difference
        // is not reported as disagreement.
        const gs::Quaternion& a = ply.rotations[i];
        gs::Quaternion b = spz.rotations[i];
        const float dot =
            a.real * b.real + a.i * b.i + a.j * b.j + a.k * b.k;
        if (dot < 0.0f) {
            b.real = -b.real;
            b.i = -b.i;
            b.j = -b.j;
            b.k = -b.k;
        }
        CheckClose(a.real, b.real, tolerance.rotation, "rotation.real", i);
        CheckClose(a.i, b.i, tolerance.rotation, "rotation.i", i);
        CheckClose(a.j, b.j, tolerance.rotation, "rotation.j", i);
        CheckClose(a.k, b.k, tolerance.rotation, "rotation.k", i);

        CheckClose(ply.dcCoefficients[i].x, spz.dcCoefficients[i].x,
                   tolerance.dc, "dc.r", i);
        CheckClose(ply.dcCoefficients[i].y, spz.dcCoefficients[i].y,
                   tolerance.dc, "dc.g", i);
        CheckClose(ply.dcCoefficients[i].z, spz.dcCoefficients[i].z,
                   tolerance.dc, "dc.b", i);
    }

    // Rest coefficients are Gaussian-major: index i * D + k. Comparing them
    // flat pins the ordering as well as the values — a per-Gaussian stride
    // error or a coefficient transpose shows up here.
    for (std::size_t n = 0; n < ply.restCoefficients.size(); ++n) {
        CheckClose(ply.restCoefficients[n].x, spz.restCoefficients[n].x,
                   tolerance.sh, "rest.r", n);
        CheckClose(ply.restCoefficients[n].y, spz.restCoefficients[n].y,
                   tolerance.sh, "rest.g", n);
        CheckClose(ply.restCoefficients[n].z, spz.restCoefficients[n].z,
                   tolerance.sh, "rest.b", n);
    }
}

void TestPair(
    const char* label,
    const char* plyFixture,
    const char* spzFixture,
    const Tolerance& tolerance,
    std::size_t expectedCount,
    int expectedDegree)
{
    const int before = failures;

    gs::GaussianCloudData ply;
    gs::GaussianCloudData spz;
    if (!Decode(plyFixture, spzFixture, &ply, &spz)) {
        return;
    }
    CheckContract(ply, plyFixture);
    CheckContract(spz, spzFixture);

    // Pin the shape independently of the comparison, so a pair that agrees
    // because both sides decoded nothing still fails.
    CHECK(ply.gaussianCount == expectedCount);
    CHECK(ply.shDegree == expectedDegree);
    CHECK(!ply.restCoefficients.empty());

    CompareClouds(ply, spz, tolerance);

    if (failures == before) {
        std::cout << "  ok  " << label << " (" << tolerance.name << ")\n";
    }
}

} // namespace

int main()
{
    // Degree 3, on the quantization grid. Every rest coefficient is present,
    // so all 15 RUB->RDF sign flips are compared against PLY, which applies
    // none of them.
    TestPair(
        "degree-3 exact, SPZ v2 (first-three rotations)",
        "equiv-degree3-exact-binary-le.ply",
        "equiv-degree3-exact-v2.spz",
        kExact, 4, 3);

    // The same PLY against the same model stored as SPZ v3. The only
    // difference from the pair above is the rotation encoding, so a failure
    // here isolates the smallest-three path.
    TestPair(
        "degree-3 exact, SPZ v3 (smallest-three rotations)",
        "equiv-degree3-exact-binary-le.ply",
        "equiv-degree3-exact-v3.spz",
        kExact, 4, 3);

    // Arbitrary values between quantization points: the envelope must hold
    // for input that was not chosen to round-trip.
    TestPair(
        "degree-1 off-grid, SPZ v2",
        "equiv-degree1-offgrid-binary-le.ply",
        "equiv-degree1-offgrid-v2.spz",
        kEnvelope, 3, 1);

    if (failures != 0) {
        std::cerr << failures << " equivalence check(s) failed\n";
        return 1;
    }
    std::cout << "all equivalence checks passed\n";
    return 0;
}
