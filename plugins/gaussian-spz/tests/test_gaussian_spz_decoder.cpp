// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSpzDecoder.h"
#include "io/GaussianSpzDiagnostics.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"
#include "openstrata/gs/testing/CloudContract.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace gs = openstrata::gs;
namespace gsspz = openstrata::gs::spz;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

std::string Fixture(const char* name)
{
    return (std::filesystem::path(GAUSSIAN_SPZ_FIXTURE_DIR) / name).string();
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

// The known source values encoded by tools/generate_fixtures.py
// (decode-degree1-v2.spz). Positions and log scales land on exact
// quantization points. SPZ's native RUB frame is the model's reference frame
// (ADR 0001), so the decoder reports these values without any conversion.
void TestDegree1FullPipeline()
{
    const gsspz::GaussianSpzDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(decoder.Decode(
        Fixture("decode-degree1-v2.spz"), &cloud, &warnings, &error));
    CHECK(error.empty());
    CHECK(warnings.empty());
    CheckContract(cloud);

    CHECK(cloud.gaussianCount == 2);
    CHECK(cloud.shDegree == 1);

    // Positions: source RUB (1, 2, -0.5) and (-3, 0.25, 4), decoded verbatim.
    CheckClose(cloud.positions[0].x, 1.0f, 1e-4f, "pos0.x");
    CheckClose(cloud.positions[0].y, 2.0f, 1e-4f, "pos0.y");
    CheckClose(cloud.positions[0].z, -0.5f, 1e-4f, "pos0.z");
    CheckClose(cloud.positions[1].x, -3.0f, 1e-4f, "pos1.x");
    CheckClose(cloud.positions[1].y, 0.25f, 1e-4f, "pos1.y");
    CheckClose(cloud.positions[1].z, 4.0f, 1e-4f, "pos1.z");

    // Scales: exp of the log scales.
    CheckClose(cloud.scales[0].x, 1.0f, 1e-4f, "scale0.x");
    CheckClose(cloud.scales[0].y, std::exp(1.0f), 1e-3f, "scale0.y");
    CheckClose(cloud.scales[0].z, std::exp(-1.0f), 1e-4f, "scale0.z");
    CheckClose(cloud.scales[1].x, std::exp(0.5f), 1e-3f, "scale1.x");
    CheckClose(cloud.scales[1].y, std::exp(-0.5f), 1e-4f, "scale1.y");
    CheckClose(cloud.scales[1].z, 1.0f, 1e-4f, "scale1.z");

    // Opacity: byte / 255, already in [0, 1].
    CheckClose(cloud.opacities[0], 0.8f, 1e-6f, "opacity0");
    CheckClose(cloud.opacities[1], 0.6f, 1e-6f, "opacity1");

    // DC coefficients: dequantized band 0.
    CheckClose(cloud.dcCoefficients[0].x, 0.0f, 0.02f, "dc0.r");
    CheckClose(cloud.dcCoefficients[0].y, 0.5f, 0.02f, "dc0.g");
    CheckClose(cloud.dcCoefficients[0].z, -0.5f, 0.02f, "dc0.b");
    CheckClose(cloud.dcCoefficients[1].x, 0.9f, 0.02f, "dc1.r");
    CheckClose(cloud.dcCoefficients[1].y, -0.9f, 0.02f, "dc1.g");
    CheckClose(cloud.dcCoefficients[1].z, 0.0f, 0.02f, "dc1.b");

    // Rotations reach the model scalar-first and normalized: the source
    // identity and the 90-degree rotation about +X, unconverted.
    CheckClose(cloud.rotations[0].real, 1.0f, 1e-2f, "rot0.w");
    CheckClose(cloud.rotations[0].i, 0.0f, 1e-2f, "rot0.x");
    CheckClose(cloud.rotations[0].j, 0.0f, 1e-2f, "rot0.y");
    CheckClose(cloud.rotations[0].k, 0.0f, 1e-2f, "rot0.z");
    CheckClose(cloud.rotations[1].real, 0.70710678f, 1e-2f, "rot1.w");
    CheckClose(cloud.rotations[1].i, 0.70710678f, 1e-2f, "rot1.x");
    CheckClose(cloud.rotations[1].j, 0.0f, 1e-2f, "rot1.y");
    CheckClose(cloud.rotations[1].k, 0.0f, 1e-2f, "rot1.z");

    // Rest SH: Gaussian-major RGB triples, dequantized source values with no
    // sign conversion. A coefficient reordering changes these exact values;
    // TestDegree3ShStride covers bands 2 and 3.
    const std::vector<gs::Float3> expectedRest = {
        {0.1f, 0.2f, 0.3f},     // P0 coef0
        {-0.1f, -0.2f, -0.3f},  // P0 coef1
        {0.4f, -0.4f, 0.5f},    // P0 coef2
        {0.05f, -0.05f, 0.15f}, // P1 coef0
        {0.25f, 0.35f, -0.45f}, // P1 coef1
        {-0.6f, 0.6f, 0.1f},    // P1 coef2
    };
    CHECK(cloud.restCoefficients.size() == expectedRest.size());
    for (std::size_t i = 0; i < expectedRest.size(); ++i) {
        const std::string at = "rest[" + std::to_string(i) + "]";
        CheckClose(cloud.restCoefficients[i].x, expectedRest[i].x, 0.01f,
            (at + ".r").c_str());
        CheckClose(cloud.restCoefficients[i].y, expectedRest[i].y, 0.01f,
            (at + ".g").c_str());
        CheckClose(cloud.restCoefficients[i].z, expectedRest[i].z, 0.01f,
            (at + ".b").c_str());
    }
}

// Mirrors tools/generate_fixtures.py sh_triple(): the source RGB for rest
// coefficient k of a point in decode-degree3-v2.spz.
gs::Float3 SourceShTriple(std::size_t point, std::size_t k)
{
    const float a = static_cast<float>(k + 1) / 32.0f;
    const float b = static_cast<float>(k + 2) / 32.0f;
    const float c = static_cast<float>(k + 3) / 64.0f;
    return point == 0 ? gs::Float3{a, -b, c} : gs::Float3{-a, c, -b};
}

// A degree-1 fixture only reaches band 1, leaving the degree-3 Gaussian
// stride unguarded. This covers every rest coefficient of both points; the
// decoder must reproduce the source values exactly (no frame conversion,
// ADR 0001 — the Y/Z sign table now lives with FlipYZAxes in gaussianCore).
void TestDegree3ShStride()
{
    const gsspz::GaussianSpzDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(
        Fixture("decode-degree3-v2.spz"), &cloud, nullptr, &error));
    CHECK(error.empty());
    CheckContract(cloud);
    CHECK(cloud.gaussianCount == 2);
    CHECK(cloud.shDegree == 3);
    CHECK(cloud.restCoefficients.size() == 2 * 15);
    if (cloud.restCoefficients.size() != 2 * 15) {
        return;
    }

    // Source values are exact multiples of the 1/128 quantization step, so
    // the only error left is float rounding.
    for (std::size_t point = 0; point < 2; ++point) {
        for (std::size_t k = 0; k < 15; ++k) {
            const gs::Float3 source = SourceShTriple(point, k);
            const gs::Float3& decoded = cloud.restCoefficients[point * 15 + k];
            const std::string at =
                "degree3 rest[" + std::to_string(point) + "][" +
                std::to_string(k) + "]";
            CheckClose(decoded.x, source.x, 1e-4f, (at + ".r").c_str());
            CheckClose(decoded.y, source.y, 1e-4f, (at + ".g").c_str());
            CheckClose(decoded.z, source.z, 1e-4f, (at + ".b").c_str());
        }
    }
}

// v1 stores float16 positions and a first-three rotation. Source RUB position
// (1.5, -2.5, 0.75), decoded verbatim.
void TestVersion1Float16Positions()
{
    const gsspz::GaussianSpzDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(Fixture("decode-v1.spz"), &cloud, nullptr, &error));
    CHECK(error.empty());
    CheckContract(cloud);
    CHECK(cloud.gaussianCount == 1);
    CHECK(cloud.shDegree == 0);
    CheckClose(cloud.positions[0].x, 1.5f, 1e-3f, "v1 pos.x");
    CheckClose(cloud.positions[0].y, -2.5f, 1e-3f, "v1 pos.y");
    CheckClose(cloud.positions[0].z, 0.75f, 1e-3f, "v1 pos.z");
    // Source rotation 45 deg about +X (w=cos22.5, x=sin22.5).
    CheckClose(cloud.rotations[0].real, 0.92387953f, 1e-2f, "v1 rot.w");
    CheckClose(cloud.rotations[0].i, 0.38268343f, 1e-2f, "v1 rot.x");
    CheckClose(cloud.rotations[0].j, 0.0f, 1e-2f, "v1 rot.y");
    CheckClose(cloud.rotations[0].k, 0.0f, 1e-2f, "v1 rot.z");
}

// v3 stores the smallest-three rotation. The source quaternion's largest
// component is x (index 0, not w), exercising the largest-index handling.
void TestVersion3SmallestThreeRotation()
{
    const gsspz::GaussianSpzDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(Fixture("decode-v3.spz"), &cloud, nullptr, &error));
    CHECK(error.empty());
    CheckContract(cloud);
    CHECK(cloud.gaussianCount == 1);

    // Source RUB position (2, -1, 0.5), decoded verbatim.
    CheckClose(cloud.positions[0].x, 2.0f, 1e-4f, "v3 pos.x");
    CheckClose(cloud.positions[0].y, -1.0f, 1e-4f, "v3 pos.y");
    CheckClose(cloud.positions[0].z, 0.5f, 1e-4f, "v3 pos.z");

    // Source quaternion (w,x,y,z) = normalize(0.2, 0.8, 0.4, 0.4). The
    // 10-bit encoding is coarser than v2, so the tolerance is wider.
    const float norm = std::sqrt(0.2f * 0.2f + 0.8f * 0.8f + 0.4f * 0.4f + 0.4f * 0.4f);
    CheckClose(cloud.rotations[0].real, 0.2f / norm, 2e-2f, "v3 rot.w");
    CheckClose(cloud.rotations[0].i, 0.8f / norm, 2e-2f, "v3 rot.x");
    CheckClose(cloud.rotations[0].j, 0.4f / norm, 2e-2f, "v3 rot.y");
    CheckClose(cloud.rotations[0].k, 0.4f / norm, 2e-2f, "v3 rot.z");
}

void TestMetadataOnly()
{
    const gsspz::GaussianSpzDecoder decoder;
    gsspz::GaussianSpzMetadata metadata;
    std::string error;
    CHECK(decoder.DecodeMetadata(
        Fixture("decode-degree1-v2.spz"), &metadata, &error));
    CHECK(error.empty());
    CHECK(metadata.gaussianCount == 2);
    CHECK(metadata.shDegree == 1);

    // Metadata must not promise a decode it would then reject: degree 4 fails
    // here with the same unsupported-degree code as Decode().
    CHECK(!decoder.DecodeMetadata(
        Fixture("decode-degree4-v2.spz"), &metadata, &error));
    CHECK(HasCode(error, gsspz::diag::kUnsupportedShDegree));
}

void TestDecodeFailure(const char* fixture, const char* code)
{
    const gsspz::GaussianSpzDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(!decoder.Decode(Fixture(fixture), &cloud, nullptr, &error));
    if (!HasCode(error, code)) {
        std::cerr << fixture << ": expected " << code << ", got: "
                  << error << "\n";
        ++failures;
    }
}

// Container-level failures propagate through the decoder unchanged: the
// decoder does not restate the reader's diagnostics.
void TestContainerFailuresPropagate()
{
    TestDecodeFailure("version-5.spz", gsspz::diag::kUnsupportedVersion);
    TestDecodeFailure("plaintext-v4.spz", gsspz::diag::kUnsupportedVersion);
    TestDecodeFailure("empty-points-v2.spz", gsspz::diag::kEmptyPointSet);
    TestDecodeFailure("not-spz.spz", gsspz::diag::kNotSpzContainer);
    TestDecodeFailure(
        "truncated-payload-v2.spz", gsspz::diag::kTruncatedContainer);
}

void TestSemanticFailures()
{
    TestDecodeFailure("decode-degree4-v2.spz", gsspz::diag::kUnsupportedShDegree);
    TestDecodeFailure("decode-nonfinite-v1.spz", gsspz::diag::kNonFinitePosition);
}

void TestWarningsForIgnoredData()
{
    const gsspz::GaussianSpzDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    // extensions-v2.spz is antialiased (0x1) + extensions (0x2); both are
    // ignored with a warning, and neither prevents a successful decode.
    CHECK(decoder.Decode(
        Fixture("extensions-v2.spz"), &cloud, &warnings, &error));
    CHECK(error.empty());
    bool sawExtensions = false;
    bool sawAntialiased = false;
    for (const std::string& warning : warnings) {
        sawExtensions = sawExtensions ||
            HasCode(warning, gsspz::diag::kExtensionsIgnored);
        sawAntialiased = sawAntialiased ||
            HasCode(warning, gsspz::diag::kAntialiasedFlagIgnored);
    }
    CHECK(sawExtensions);
    CHECK(sawAntialiased);
}

void TestInternalMisuse()
{
    const gsspz::GaussianSpzDecoder decoder;
    std::string error;
    CHECK(!decoder.Decode(Fixture("decode-v1.spz"), nullptr, nullptr, &error));
    CHECK(HasCode(error, gsspz::diag::kInternalError));
    CHECK(!decoder.DecodeMetadata(Fixture("decode-v1.spz"), nullptr, &error));
    CHECK(HasCode(error, gsspz::diag::kInternalError));
}

// The decoder's half of the shared import-statistics seam
// (GaussianImportStats.h): filled on request, consistent with the decoded
// cloud, and carrying this format's identity.
void TestImportStats()
{
    const gsspz::GaussianSpzDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    gs::GaussianImportStats stats;
    const std::string path = Fixture("decode-degree1-v2.spz");
    CHECK(decoder.Decode(path, &cloud, &warnings, &error, &stats));
    CHECK(stats.sourceFormat == gsspz::kSourceFormatToken);
    CHECK(stats.sourceVersion == "2");
    CHECK(stats.gaussianCount == cloud.gaussianCount);
    CHECK(stats.shDegree == cloud.shDegree);
    CHECK(stats.sourceBytes ==
        static_cast<std::uint64_t>(std::filesystem::file_size(path)));
    CHECK(stats.decodedBytes == gs::ComputeDecodedByteSize(cloud));
    CHECK(stats.readSeconds >= 0.0 && stats.decodeSeconds >= 0.0);
    // The decoder does not time authoring and does not compute bounds; both
    // stay with the file-format caller.
    CHECK(stats.authorSeconds == 0.0);
    CHECK(!stats.hasBounds);

    gs::GaussianImportStats v3Stats;
    CHECK(decoder.Decode(
        Fixture("decode-v3.spz"), &cloud, &warnings, &error, &v3Stats));
    CHECK(v3Stats.sourceVersion == "3");
}

} // namespace

int main()
{
    TestDegree1FullPipeline();
    TestDegree3ShStride();
    TestVersion1Float16Positions();
    TestVersion3SmallestThreeRotation();
    TestMetadataOnly();
    TestContainerFailuresPropagate();
    TestSemanticFailures();
    TestWarningsForIgnoredData();
    TestInternalMisuse();
    TestImportStats();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
