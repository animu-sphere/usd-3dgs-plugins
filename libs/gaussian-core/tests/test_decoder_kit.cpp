// SPDX-License-Identifier: Apache-2.0
//
// The decoder test kit's own coverage, and the v0.4.0 completion criterion it
// exists for: a minimal mock decoder targets the shared contract using no PLY
// or SPZ code -- only GAUSSIAN_MODEL_CONTRACT.md and gaussianCore. If this
// file ever needs a bundle header, the contract has failed its purpose.
//
// The mock format ("MockSplat") deliberately stores every representation the
// contract forbids in the model -- an RDF frame, log scales, opacity logits,
// vector-first quaternions, channel-major SH -- so decoding it exercises each
// conversion a real format needs, through the same shared helpers the real
// decoders use.

#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/GaussianSizeMath.h"
#include "openstrata/gs/testing/CloudContract.h"
#include "openstrata/gs/testing/DecoderTestKit.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace gs = openstrata::gs;
namespace kit = openstrata::gs::testing;

namespace {

int failures = 0;

#define CHECK(expr) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
        ++failures; \
    } } while (false)

#define CHECK_MSG(expr, context) \
    do { if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr \
                  << " [" << (context) << "]\n"; \
        ++failures; \
    } } while (false)

// --- The mock format --------------------------------------------------------

// In-memory MockSplat document: one float stream per attribute, every value
// in a format-native encoding the decoder must undo.
struct MockSplatDocument {
    std::size_t count = 0;
    int shDegree = 0;
    // RDF (Y-down) positions, x/y/z interleaved per Gaussian.
    std::vector<float> positionsRdf;
    // Natural-log scales.
    std::vector<float> logScales;
    // Vector-first (i, j, k, real) quaternions in the RDF frame, unnormalized.
    std::vector<float> rotationsVectorFirst;
    // Pre-sigmoid opacity logits.
    std::vector<float> opacityLogits;
    // DC triples, then rest coefficients channel-major in the RDF frame:
    // all R coefficients, all G, all B (the Graphdeco PLY column layout),
    // channels outermost, coefficient innermost.
    std::vector<float> dc;
    std::vector<float> restChannelMajor;
};

// Inverse sigmoid for building fixtures; the mock encoder is as much a part
// of the test as the decoder, so canonical opacities round-trip exactly
// enough for the kit's default tolerances.
float Logit(float opacity)
{
    return std::log(opacity / (1.0f - opacity));
}

// Encodes a canonical (model-space) cloud into the mock format's native
// representations: the inverse of every conversion the decoder must apply.
MockSplatDocument EncodeMockSplat(const gs::GaussianCloudData& model)
{
    // Work on a copy converted into the format's RDF frame; FlipYZAxes is its
    // own inverse, so the decoder's RDF->RUB conversion restores the model.
    gs::GaussianCloudData source = model;
    gs::FlipYZAxes(&source);

    MockSplatDocument document;
    document.count = source.gaussianCount;
    document.shDegree = source.shDegree;
    const std::size_t restPerGaussian =
        source.CoefficientsPerGaussian() - 1;
    for (std::size_t i = 0; i < source.gaussianCount; ++i) {
        const gs::Float3& p = source.positions[i];
        document.positionsRdf.insert(
            document.positionsRdf.end(), {p.x, p.y, p.z});
        const gs::Float3& s = source.scales[i];
        document.logScales.insert(
            document.logScales.end(),
            {std::log(s.x), std::log(s.y), std::log(s.z)});
        const gs::Quaternion& q = source.rotations[i];
        // Vector-first on disk, and scaled by 2 so the decoder must
        // normalize rather than pass through.
        document.rotationsVectorFirst.insert(
            document.rotationsVectorFirst.end(),
            {2.0f * q.i, 2.0f * q.j, 2.0f * q.k, 2.0f * q.real});
        document.opacityLogits.push_back(Logit(source.opacities[i]));
        const gs::Float3& dc = source.dcCoefficients[i];
        document.dc.insert(document.dc.end(), {dc.x, dc.y, dc.z});
    }
    // Channel-major rest: channel outermost, then Gaussian, then coefficient
    // innermost -- one contiguous [Gaussian][coefficient] block per channel,
    // matching the PLY f_rest_* column convention (all R coefficients, then
    // all G, then all B). The decoder transposes this back to Gaussian-major.
    for (int channel = 0; channel < 3; ++channel) {
        for (std::size_t i = 0; i < source.gaussianCount; ++i) {
            for (std::size_t c = 0; c < restPerGaussian; ++c) {
                const gs::Float3& value =
                    source.restCoefficients[i * restPerGaussian + c];
                document.restChannelMajor.push_back(
                    channel == 0 ? value.x : channel == 1 ? value.y : value.z);
            }
        }
    }
    return document;
}

// The mock decoder: MockSplatDocument -> GaussianCloudData, written against
// the contract alone. Structure mirrors what the guide asks of a real
// decoder: overflow-checked allocation, shared conversion helpers, frame
// conversion after arrays are built, shared validation last.
bool DecodeMockSplat(
    const MockSplatDocument& document,
    gs::GaussianCloudData* cloud,
    std::string* error)
{
    const auto fail = [error](const char* message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    gs::GaussianCloudData result;
    result.gaussianCount = document.count;
    result.shDegree = document.shDegree;

    // Contract §3, maximum count and overflow: derived sizes go through the
    // shared checked arithmetic before any allocation.
    std::size_t restCount = 0;
    if (!gs::ComputeRestCoefficientCount(
            document.count, document.shDegree, &restCount)) {
        return fail("rest-coefficient count overflows");
    }
    if (!gs::TryResize(&result.positions, document.count) ||
        !gs::TryResize(&result.scales, document.count) ||
        !gs::TryResize(&result.rotations, document.count) ||
        !gs::TryResize(&result.opacities, document.count) ||
        !gs::TryResize(&result.dcCoefficients, document.count) ||
        !gs::TryResize(&result.restCoefficients, restCount)) {
        return fail("model arrays could not be allocated");
    }

    const std::size_t restPerGaussian =
        result.CoefficientsPerGaussian() - 1;
    for (std::size_t i = 0; i < document.count; ++i) {
        result.positions[i] = {
            document.positionsRdf[i * 3],
            document.positionsRdf[i * 3 + 1],
            document.positionsRdf[i * 3 + 2]};
        if (!gs::DecodeLogScale(
                {document.logScales[i * 3],
                 document.logScales[i * 3 + 1],
                 document.logScales[i * 3 + 2]},
                &result.scales[i])) {
            return fail("log scale does not decode");
        }
        // Reorder vector-first to the model's scalar-first convention, then
        // normalize through the shared helper.
        const float* r = document.rotationsVectorFirst.data() + i * 4;
        if (!gs::NormalizeQuaternion(
                {r[3], r[0], r[1], r[2]}, &result.rotations[i])) {
            return fail("quaternion is not normalizable");
        }
        result.opacities[i] = gs::Sigmoid(document.opacityLogits[i]);
        result.dcCoefficients[i] = {
            document.dc[i * 3],
            document.dc[i * 3 + 1],
            document.dc[i * 3 + 2]};
    }
    // Channel-major to Gaussian-major transpose.
    const std::size_t channelStride = document.count * restPerGaussian;
    for (std::size_t i = 0; i < document.count; ++i) {
        for (std::size_t c = 0; c < restPerGaussian; ++c) {
            const std::size_t column = i * restPerGaussian + c;
            result.restCoefficients[column] = {
                document.restChannelMajor[column],
                document.restChannelMajor[channelStride + column],
                document.restChannelMajor[2 * channelStride + column]};
        }
    }

    // The mock format's conventional frame is RDF; the model's is RUB
    // (contract §2, ADR 0001).
    gs::FlipYZAxes(&result);

    std::string validationError;
    if (!gs::ValidateGaussianCloud(result, &validationError)) {
        if (error) {
            *error = validationError;
        }
        return false;
    }
    *cloud = std::move(result);
    return true;
}

// --- Tests ------------------------------------------------------------------

// The criterion itself: encode both canonical clouds into the mock format,
// decode back, and hold the result to the contract checker and the kit's
// comparison. Passing proves the contract documents everything a decoder
// needs; no PLY or SPZ source was consulted.
void TestMockDecoderRoundTrip()
{
    for (const bool multi : {false, true}) {
        const gs::GaussianCloudData expected = multi
            ? kit::MakeCanonicalMultiGaussianCloud()
            : kit::MakeCanonicalOneGaussianCloud();
        const MockSplatDocument document = EncodeMockSplat(expected);

        gs::GaussianCloudData decoded;
        std::string error;
        CHECK_MSG(DecodeMockSplat(document, &decoded, &error), error);
        for (const std::string& violation :
             kit::CheckCloudContract(decoded)) {
            CHECK_MSG(false, violation);
        }
        for (const std::string& mismatch :
             kit::CompareClouds(decoded, expected)) {
            CHECK_MSG(false, mismatch);
        }
    }
}

// Each invalid model-level case must fail the shared gate; the mock decoder
// cannot smuggle one into the model because validation is its last step.
void TestInvalidCasesAreRejected()
{
    const auto cases = kit::MakeInvalidCloudCases();
    CHECK(cases.size() >= 10);
    for (const auto& invalid : cases) {
        std::string error;
        CHECK_MSG(!gs::ValidateGaussianCloud(invalid.cloud, &error),
            invalid.name);
        CHECK_MSG(!kit::CheckCloudContract(invalid.cloud).empty(),
            invalid.name);
    }
}

// The comparison must distinguish the three orderings whose array lengths
// are identical: Gaussian-major versus coefficient-major point order,
// coefficient transposition, and channel transposition.
void TestComparisonDistinguishesOrderings()
{
    const gs::GaussianCloudData expected =
        kit::MakeCanonicalMultiGaussianCloud();
    const std::size_t restPerGaussian =
        expected.CoefficientsPerGaussian() - 1;

    CHECK(kit::CompareClouds(expected, expected).empty());

    // Swap two Gaussians' rest blocks: point-order confusion.
    gs::GaussianCloudData pointSwapped = expected;
    for (std::size_t c = 0; c < restPerGaussian; ++c) {
        std::swap(pointSwapped.restCoefficients[c],
            pointSwapped.restCoefficients[restPerGaussian + c]);
    }
    CHECK(!kit::CompareClouds(pointSwapped, expected).empty());

    // Transpose (gaussian, coefficient): coefficient-major layout.
    gs::GaussianCloudData transposed = expected;
    for (std::size_t i = 0; i < expected.gaussianCount; ++i) {
        for (std::size_t c = 0; c < restPerGaussian; ++c) {
            transposed.restCoefficients[c * expected.gaussianCount + i] =
                expected.restCoefficients[i * restPerGaussian + c];
        }
    }
    CHECK(!kit::CompareClouds(transposed, expected).empty());

    // Rotate RGB channels: channel-order confusion.
    gs::GaussianCloudData channelRotated = expected;
    for (gs::Float3& value : channelRotated.restCoefficients) {
        value = {value.y, value.z, value.x};
    }
    CHECK(!kit::CompareClouds(channelRotated, expected).empty());
}

void TestQuaternionSignEquivalence()
{
    const gs::Quaternion q = {0.5f, 0.5f, 0.5f, 0.5f};
    const gs::Quaternion negated = {-0.5f, -0.5f, -0.5f, -0.5f};
    const gs::Quaternion other = {0.5f, 0.5f, -0.5f, 0.5f};
    CHECK(kit::QuaternionsEquivalent(q, q, 1.0e-6f));
    CHECK(kit::QuaternionsEquivalent(q, negated, 1.0e-6f));
    CHECK(!kit::QuaternionsEquivalent(q, other, 1.0e-6f));
}

// A cloud whose positions differ within per-field tolerance but whose scales
// shift the derived bound must trip the extent comparison, and the derived
// extent must match ComputeCloudExtent exactly.
void TestExtentComparison()
{
    const gs::GaussianCloudData expected =
        kit::MakeCanonicalOneGaussianCloud();

    gs::Float3 minimum, maximum;
    CHECK(gs::ComputeCloudExtent(
        expected.positions.data(), expected.scales.data(),
        expected.gaussianCount, &minimum, &maximum));
    const float radius = 3.0f * 0.04f;
    CHECK(std::fabs(minimum.x - (0.5f - radius)) <= 1.0e-6f);
    CHECK(std::fabs(maximum.z - (2.0f + radius)) <= 1.0e-6f);

    gs::GaussianCloudData widened = expected;
    widened.scales[0].z = 10.0f;
    CHECK(!kit::CompareClouds(widened, expected).empty());

    // Zero count and non-finite bounds are not computable.
    CHECK(!gs::ComputeCloudExtent(
        expected.positions.data(), expected.scales.data(), 0,
        &minimum, &maximum));
}

void TestSizeMath()
{
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    std::size_t out = 0;
    CHECK(gs::CheckedMulSize(6, 7, &out) && out == 42);
    CHECK(gs::CheckedMulSize(0, maximum, &out) && out == 0);
    CHECK(!gs::CheckedMulSize(maximum / 2 + 1, 2, &out));
    CHECK(gs::CheckedAddSize(maximum - 1, 1, &out) && out == maximum);
    CHECK(!gs::CheckedAddSize(maximum, 1, &out));

    // Rest count: (D+1)^2 - 1 per Gaussian, checked.
    CHECK(gs::ComputeRestCoefficientCount(2, 3, &out) && out == 30);
    CHECK(gs::ComputeRestCoefficientCount(5, 0, &out) && out == 0);
    CHECK(!gs::ComputeRestCoefficientCount(maximum / 2, 3, &out));
    CHECK(!gs::ComputeRestCoefficientCount(1, gs::kMaxShDegree + 1, &out));
    CHECK(!gs::ComputeRestCoefficientCount(1, -1, &out));

    // TryResize surfaces an impossible allocation as false, not a throw.
    std::vector<gs::Float3> array;
    CHECK(gs::TryResize(&array, 3) && array.size() == 3);
    CHECK(!gs::TryResize(&array, array.max_size()));
    CHECK(!gs::TryResize<gs::Float3>(nullptr, 1));
}

void TestImportStatsSeam()
{
    const gs::GaussianCloudData cloud =
        kit::MakeCanonicalMultiGaussianCloud();
    // 3 Gaussians, degree 3: exact semantic bytes, independent of capacity.
    const std::uint64_t expectedBytes =
        3ull * (12 + 12 + 16 + 4 + 12) + 45ull * 12;
    CHECK(gs::ComputeDecodedByteSize(cloud) == expectedBytes);

    gs::GaussianImportStats stats;
    stats.sourceFormat = "MockSplat";
    stats.sourceVersion = "1";
    stats.gaussianCount = cloud.gaussianCount;
    stats.shDegree = cloud.shDegree;
    stats.sourceBytes = 1234;
    stats.decodedBytes = gs::ComputeDecodedByteSize(cloud);
    stats.readSeconds = 0.25;
    stats.decodeSeconds = 0.5;
    stats.authorSeconds = 0.125;
    stats.hasBounds = gs::ComputeCloudExtent(
        cloud.positions.data(), cloud.scales.data(), cloud.gaussianCount,
        &stats.boundsMinimum, &stats.boundsMaximum);
    CHECK(stats.hasBounds);

    const std::string line = gs::FormatImportStats(stats);
    CHECK(line.find("format=\"MockSplat\"") != std::string::npos);
    CHECK(line.find("version=\"1\"") != std::string::npos);
    CHECK(line.find("gaussians=3") != std::string::npos);
    CHECK(line.find("shDegree=3") != std::string::npos);
    CHECK(line.find("sourceBytes=1234") != std::string::npos);
    CHECK(line.find("decodedBytes=" + std::to_string(expectedBytes)) !=
        std::string::npos);
    CHECK(line.find("boundsMin=") != std::string::npos);
    CHECK(line.find("readSeconds=0.25") != std::string::npos);

    // Without bounds the bounds keys are absent, not zero-filled.
    stats.hasBounds = false;
    CHECK(gs::FormatImportStats(stats).find("boundsMin=") ==
        std::string::npos);
}

} // namespace

int main()
{
    TestMockDecoderRoundTrip();
    TestInvalidCasesAreRejected();
    TestComparisonDistinguishesOrderings();
    TestQuaternionSignEquivalence();
    TestExtentComparison();
    TestSizeMath();
    TestImportStatsSeam();
    return failures == 0 ? 0 : 1;
}
