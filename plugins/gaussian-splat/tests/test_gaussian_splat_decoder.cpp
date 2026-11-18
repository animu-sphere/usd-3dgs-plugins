// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianSplatDecoder.h"
#include "io/GaussianSplatDiagnostics.h"
#include "io/SplatReader.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianFileIO.h"
#include "openstrata/gs/GaussianImportStats.h"
#include "openstrata/gs/testing/CloudContract.h"
#include "openstrata/gs/testing/DecoderTestKit.h"
#include "openstrata/gs/testing/Utf8TestPaths.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace gs = openstrata::gs;
namespace gssplat = openstrata::gs::splat;

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n";       \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

std::string Fixture(const char* name)
{
    return (std::filesystem::path(GAUSSIAN_SPLAT_FIXTURE_DIR) / name).string();
}

// Third-party reference files that are not packaged (tests/reference).
std::string Reference(const char* name)
{
    return (std::filesystem::path(GAUSSIAN_SPLAT_REFERENCE_DIR) / name)
        .string();
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

void CheckFloat3(const gs::Float3& actual, float x, float y, float z,
                 float tolerance, const std::string& what)
{
    CheckClose(actual.x, x, tolerance, (what + ".x").c_str());
    CheckClose(actual.y, y, tolerance, (what + ".y").c_str());
    CheckClose(actual.z, z, tolerance, (what + ".z").c_str());
}

void CheckContract(const gs::GaussianCloudData& cloud)
{
    for (const std::string& violation :
         gs::testing::CheckCloudContract(cloud)) {
        std::cerr << "contract: " << violation << "\n";
        ++failures;
    }
}

// The two records of decode-known.splat (tools/generate_fixtures.py). The
// file stores the Graphdeco RDF frame, so the model sees Y and Z negated on
// positions and on the quaternion's vector part (ADR 0001). The DC values
// are (byte / 255 - 0.5) / SH_C0, computed independently of the decoder.
void TestKnownValues()
{
    const gssplat::GaussianSplatDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(decoder.Decode(Fixture("decode-known.splat"), &cloud, &warnings,
                         &error));
    CHECK(error.empty());
    CHECK(warnings.empty());
    CheckContract(cloud);
    CHECK(cloud.gaussianCount == 2);
    CHECK(cloud.shDegree == 0);
    CHECK(cloud.restCoefficients.empty());
    if (cloud.gaussianCount != 2) {
        return;
    }

    // float32 positions and linear scales are stored exactly.
    CheckFloat3(cloud.positions[0], 1.0f, -2.0f, 0.5f, 0.0f, "pos0");
    CheckFloat3(cloud.positions[1], -3.0f, -0.25f, -4.0f, 0.0f, "pos1");
    CheckFloat3(cloud.scales[0], 0.5f, 1.0f, 2.0f, 0.0f, "scale0");
    CheckFloat3(cloud.scales[1], 0.01f, 0.02f, 0.04f, 0.0f, "scale1");

    CheckClose(cloud.opacities[0], 0.8f, 1e-6f, "opacity0");
    CheckClose(cloud.opacities[1], 0.6f, 1e-6f, "opacity1");

    // Colour bytes (128, 64, 200) and (0, 255, 128). Bytes 0 and 255 are the
    // ends of the representable DC range, +-0.5 / SH_C0.
    CheckFloat3(cloud.dcCoefficients[0], 0.0069508f, -0.8827515f, 1.0078659f,
                1e-5f, "dc0");
    CheckFloat3(cloud.dcCoefficients[1], -1.7724539f, 1.7724539f, 0.0069508f,
                1e-5f, "dc1");

    // Bytes (255, 128, 128, 128): w = 127/128 alone, normalized to identity.
    CheckClose(cloud.rotations[0].real, 1.0f, 1e-6f, "rot0.w");
    CheckClose(cloud.rotations[0].i, 0.0f, 1e-6f, "rot0.x");
    CheckClose(cloud.rotations[0].j, 0.0f, 1e-6f, "rot0.y");
    CheckClose(cloud.rotations[0].k, 0.0f, 1e-6f, "rot0.z");
    // Bytes (218, 128, 218, 128): 90 degrees about +Y in RDF, which is
    // 90 degrees about -Y in RUB.
    CheckClose(cloud.rotations[1].real, 0.70710678f, 1e-6f, "rot1.w");
    CheckClose(cloud.rotations[1].i, 0.0f, 1e-6f, "rot1.x");
    CheckClose(cloud.rotations[1].j, -0.70710678f, 1e-6f, "rot1.y");
    CheckClose(cloud.rotations[1].k, 0.0f, 1e-6f, "rot1.z");
}

// SplatTransform's own fixture (tests/reference/README.md): four isotropic
// Gaussians on a grid in the XZ plane of the PLY frame.
void TestReferenceWritten()
{
    const gssplat::GaussianSplatDecoder decoder;
    gs::GaussianCloudData cloud;
    std::string error;
    CHECK(decoder.Decode(Reference("splattransform-minimal.splat"), &cloud,
                         nullptr, &error));
    CHECK(error.empty());
    CheckContract(cloud);
    CHECK(cloud.gaussianCount == 4);
    if (cloud.gaussianCount != 4) {
        return;
    }
    const float grid[4][2] = {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}};
    const unsigned char red[4] = {85, 170, 85, 170};
    const unsigned char green[4] = {85, 85, 170, 170};
    for (std::size_t i = 0; i < 4; ++i) {
        const std::string at = "reference[" + std::to_string(i) + "]";
        // Stored (x, 0, z); the model negates Z (and Y, which is zero).
        CheckFloat3(cloud.positions[i], grid[i][0], 0.0f, -grid[i][1], 0.0f,
                    at + ".position");
        CheckFloat3(cloud.scales[i], 0.1f, 0.1f, 0.1f, 1e-7f, at + ".scale");
        CheckClose(cloud.opacities[i], 230.0f / 255.0f, 1e-6f,
                   (at + ".opacity").c_str());
        const auto dc = [](unsigned char byte) {
            return (static_cast<float>(byte) / 255.0f - 0.5f) /
                   0.28209479177387814f;
        };
        CheckFloat3(cloud.dcCoefficients[i], dc(red[i]), dc(green[i]), dc(128),
                    1e-6f, at + ".dc");
    }
}

// --- Decoder test kit round trip -------------------------------------------
//
// The canonical clouds of DecoderTestKit.h, encoded the way each writer
// family does and decoded back. `.splat` has no rest coefficients, so the
// multi-Gaussian cloud is compared at degree 0.

unsigned char ToByte(double value, bool round)
{
    const double clamped = std::min(255.0, std::max(0.0, value));
    return static_cast<unsigned char>(round ? std::floor(clamped + 0.5) :
                                              std::floor(clamped));
}

void PutFloat(std::vector<unsigned char>* out, float value)
{
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    for (int shift = 0; shift < 32; shift += 8) {
        out->push_back(static_cast<unsigned char>((bits >> shift) & 0xffu));
    }
}

// Model (RUB) values to a `.splat` record (RDF), per SPLAT_FORMAT.md §2.
std::vector<unsigned char> EncodeSplat(const gs::GaussianCloudData& cloud,
                                       bool round)
{
    constexpr double kShC0 = 0.28209479177387814;
    std::vector<unsigned char> out;
    for (std::size_t i = 0; i < cloud.gaussianCount; ++i) {
        const gs::Float3& p = cloud.positions[i];
        PutFloat(&out, p.x);
        PutFloat(&out, -p.y);
        PutFloat(&out, -p.z);
        const gs::Float3& s = cloud.scales[i];
        PutFloat(&out, s.x);
        PutFloat(&out, s.y);
        PutFloat(&out, s.z);
        const gs::Float3& dc = cloud.dcCoefficients[i];
        for (const float c : {dc.x, dc.y, dc.z}) {
            out.push_back(ToByte((0.5 + kShC0 * c) * 255.0, round));
        }
        out.push_back(ToByte(cloud.opacities[i] * 255.0, round));
        const gs::Quaternion& q = cloud.rotations[i];
        for (const float c : {q.real, q.i, -q.j, -q.k}) {
            out.push_back(ToByte(c * 128.0 + 128.0, round));
        }
    }
    return out;
}

void TestDecoderKitRoundTrip()
{
    gs::GaussianCloudData multi =
        gs::testing::MakeCanonicalMultiGaussianCloud();
    multi.shDegree = 0;
    multi.restCoefficients.clear();
    const gs::GaussianCloudData clouds[] = {
        gs::testing::MakeCanonicalOneGaussianCloud(), multi};

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "gaussian-splat-decoder-kit";
    std::filesystem::create_directories(directory);

    constexpr float kShC0 = 0.28209479177387814f;
    for (const bool round : {false, true}) {
        // One step for a truncating writer, half a step for a rounding one
        // (EQUIVALENCE.md). A truncated rotation component moves by at most
        // one step (1/128) before normalization, and renormalizing the
        // four-component error adds at most twice that, so 0.025 bounds each
        // component; rounding halves it (SPLAT_MAPPING.md §4).
        const float steps = round ? 0.5f : 1.0f;
        gs::testing::CloudTolerances tolerances;
        tolerances.position = 0.0f;
        tolerances.scale = 0.0f;
        tolerances.opacity = steps / 255.0f + 1e-6f;
        tolerances.shCoefficient = steps / (255.0f * kShC0) + 1e-5f;
        tolerances.rotation = 0.025f * steps;
        tolerances.extent = 1e-5f;

        for (std::size_t c = 0; c < 2; ++c) {
            const std::filesystem::path path =
                directory / ("kit-" + std::to_string(c) +
                             (round ? "-round" : "-truncate") + ".splat");
            {
                const std::vector<unsigned char> bytes =
                    EncodeSplat(clouds[c], round);
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
            }
            gs::GaussianCloudData decoded;
            std::string error;
            CHECK(gssplat::GaussianSplatDecoder().Decode(
                path.string(), &decoded, nullptr, &error));
            CheckContract(decoded);
            for (const std::string& mismatch :
                 gs::testing::CompareClouds(decoded, clouds[c], tolerances)) {
                std::cerr << path.filename().string() << ": " << mismatch
                          << "\n";
                ++failures;
            }
        }
    }

    std::error_code removed;
    std::filesystem::remove_all(directory, removed);
}

void TestMetadataOnly()
{
    const gssplat::GaussianSplatDecoder decoder;
    gssplat::GaussianSplatMetadata metadata;
    std::string error;
    CHECK(decoder.DecodeMetadata(Fixture("decode-known.splat"), &metadata,
                                 &error));
    CHECK(error.empty());
    CHECK(metadata.gaussianCount == 2);
    CHECK(metadata.shDegree == 0);

    // The metadata path reads the signature too, so it does not promise a
    // decode that Read() would refuse for a renamed file. Value errors need
    // the records and are left to the full read.
    CHECK(!decoder.DecodeMetadata(Fixture("renamed-ply.splat"), &metadata,
                                  &error));
    CHECK(HasCode(error, gssplat::diag::kForeignFormat));
    CHECK(!decoder.DecodeMetadata(Fixture("empty.splat"), &metadata, &error));
    CHECK(HasCode(error, gssplat::diag::kEmptyFile));
    CHECK(
        decoder.DecodeMetadata(Fixture("zero-scale.splat"), &metadata, &error));
    CHECK(metadata.gaussianCount == 2);
}

void TestDecodeFailure(const char* name, const char* code)
{
    const gssplat::GaussianSplatDecoder decoder;
    gs::GaussianCloudData cloud;
    cloud.gaussianCount = 99; // must stay untouched on failure
    std::string error;
    CHECK(!decoder.Decode(Fixture(name), &cloud, nullptr, &error));
    CHECK(cloud.gaussianCount == 99);
    if (!HasCode(error, code)) {
        std::cerr << name << ": expected " << code << ", got '" << error
                  << "'\n";
        ++failures;
    }
}

void TestZeroRotationWarning()
{
    const gssplat::GaussianSplatDecoder decoder;
    gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(decoder.Decode(Fixture("zero-rotation.splat"), &cloud, &warnings,
                         &error));
    CheckContract(cloud);
    CHECK(warnings.size() == 1);
    if (warnings.size() == 1) {
        CHECK(HasCode(warnings[0], gssplat::diag::kZeroRotationReplaced));
        CHECK(warnings[0].find(" 1 Gaussian(s)") != std::string::npos);
    }
    if (cloud.gaussianCount == 2) {
        CheckClose(cloud.rotations[0].real, 1.0f, 0.0f, "zero.w");
        CheckClose(cloud.rotations[0].i, 0.0f, 0.0f, "zero.x");
        CheckClose(cloud.rotations[0].j, 0.0f, 0.0f, "zero.y");
        CheckClose(cloud.rotations[0].k, 0.0f, 0.0f, "zero.z");
    }
}

void TestImportStats()
{
    gs::GaussianCloudData cloud;
    gs::GaussianImportStats stats;
    std::string error;
    CHECK(gssplat::GaussianSplatDecoder().Decode(
        Fixture("decode-known.splat"), &cloud, nullptr, &error, &stats));
    CHECK(stats.sourceFormat == gssplat::kSourceFormatToken);
    CHECK(stats.sourceVersion.empty());
    CHECK(stats.coordinateConversion ==
          gs::GaussianCoordinateConversion::RdfToRub);
    CHECK(stats.gaussianCount == 2);
    CHECK(stats.shDegree == 0);
    CHECK(stats.sourceBytes == 64);
    CHECK(stats.decodedBytes == gs::ComputeDecodedByteSize(cloud));
    CHECK(stats.readSeconds >= 0.0);
    CHECK(stats.decodeSeconds >= 0.0);
}

void TestInternalMisuse()
{
    const gssplat::GaussianSplatDecoder decoder;
    std::string error;
    CHECK(!decoder.Decode(Fixture("decode-known.splat"), nullptr, nullptr,
                          &error));
    CHECK(HasCode(error, gssplat::diag::kInternalError));
    error.clear();
    CHECK(!decoder.DecodeMetadata(Fixture("decode-known.splat"), nullptr,
                                  &error));
    CHECK(HasCode(error, gssplat::diag::kInternalError));
}

// A UTF-8 path outside every single Windows ANSI code page must open
// through every entry point (GaussianFileIO.h).
void TestNonAsciiPath()
{
    static const char* kTag = "gaussian-splat-decoder-utf8";
    const std::filesystem::path fixture = Fixture("decode-known.splat");
    const std::string directory =
        gs::testing::MakeNonAsciiFixtureDirectory(kTag, &fixture, 1);
    CHECK(!directory.empty());
    if (directory.empty()) {
        return;
    }
    const std::string path = directory + "decode-known.splat";

    const gssplat::GaussianSplatDecoder decoder;
    CHECK(decoder.CanRead(path));

    gssplat::GaussianSplatMetadata metadata;
    std::string error;
    CHECK(decoder.DecodeMetadata(path, &metadata, &error));
    CHECK(error.empty());
    CHECK(metadata.gaussianCount == 2);

    gs::GaussianCloudData cloud;
    gs::GaussianImportStats stats;
    CHECK(decoder.Decode(path, &cloud, nullptr, &error, &stats));
    CHECK(error.empty());
    CHECK(cloud.gaussianCount == 2);
    CHECK(stats.sourceBytes ==
          static_cast<std::uint64_t>(
              std::filesystem::file_size(gs::PathFromUtf8(path))));

    gs::testing::RemoveNonAsciiFixtureDirectory(kTag);
}

} // namespace

int main()
{
    TestKnownValues();
    TestReferenceWritten();
    TestDecoderKitRoundTrip();
    TestMetadataOnly();

    // File layout failures propagate from the reader unchanged.
    TestDecodeFailure("empty.splat", gssplat::diag::kEmptyFile);
    TestDecodeFailure("partial-record.splat", gssplat::diag::kPartialRecord);
    TestDecodeFailure("renamed-sog.splat", gssplat::diag::kForeignFormat);
    // Record values the model cannot hold.
    TestDecodeFailure("nonfinite-position.splat",
                      gssplat::diag::kNonFinitePosition);
    TestDecodeFailure("infinite-position.splat",
                      gssplat::diag::kNonFinitePosition);
    TestDecodeFailure("zero-scale.splat", gssplat::diag::kInvalidScale);
    TestDecodeFailure("negative-scale.splat", gssplat::diag::kInvalidScale);
    TestDecodeFailure("nonfinite-scale.splat", gssplat::diag::kInvalidScale);

    TestZeroRotationWarning();
    TestImportStats();
    TestInternalMisuse();
    TestNonAsciiPath();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
