// SPDX-License-Identifier: Apache-2.0
//
// Cross-format equivalence, PLY against SuperSplat compressed PLY: the source
// models the other suites use, plus a multi-chunk model, encoded by the
// bundle's writer-compatible encoder (tools/generate_fixtures.py), must decode
// to the same GaussianCloudData as the lossless PLY encoding.
//
// Both dialects are read by the gaussian-ply bundle, but through independent
// paths: float columns on one side, chunk-relative unorm codes, a
// smallest-three rotation, and byte SH on the other. Both store the Graphdeco
// RDF frame and channel-major rest coefficients, so this pins the compressed
// path's flip and transpose against the PLY path's.
//
// Every tolerance is half a quantization step derived from the compressed
// file's own chunk table (COMPRESSED_PLY_MAPPING.md §4, EQUIVALENCE.md §2), so
// each row is held to its own chunk's grid rather than to a global envelope.

#include "equivalence_common.h"

#include "io/GaussianPlyDecoder.h"
#include "io/PlyReader.h"
#include "openstrata/gs/GaussianCloudData.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace gs = openstrata::gs;
namespace gsply = openstrata::gs::ply;
using namespace openstrata::gs::equivalence;

namespace {

constexpr double kShC0 = 0.28209479177387814;
// The EQUIVALENCE.md rotation envelope shared with SPZ and SOG.
constexpr float kRotation = 8.0e-3f;
// Half of the 1/32 SH step, inside (-4, 4); the fixtures stay in [-1.5, 1.5].
constexpr float kShHalfStep = 1.0f / 64.0f + 1.0e-6f;

using Chunk = std::array<float, 18>;

std::vector<Chunk> ReadChunks(const std::string& path)
{
    static const char* kNames[18] = {
        "min_x",       "min_y",       "min_z",       "max_x",
        "max_y",       "max_z",       "min_scale_x", "min_scale_y",
        "min_scale_z", "max_scale_x", "max_scale_y", "max_scale_z",
        "min_r",       "min_g",       "min_b",       "max_r",
        "max_g",       "max_b",
    };
    std::vector<gsply::PlyElementRows> rows(1);
    rows[0].element = "chunk";
    rows[0].properties.assign(std::begin(kNames), std::end(kNames));
    gsply::PlyHeader header;
    std::string error;
    std::vector<Chunk> chunks;
    if (!gsply::PlyReader().ReadElementRows(path, &rows, &header, &error)) {
        std::cerr << path << ": " << error << "\n";
        ++failures;
        return chunks;
    }
    chunks.resize(rows[0].rowCount);
    for (std::size_t c = 0; c < chunks.size(); ++c) {
        std::memcpy(chunks[c].data(),
                    rows[0].bytes.data() + c * rows[0].rowStride,
                    sizeof(Chunk));
    }
    return chunks;
}

void ComparePair(const char* plyFixture, const char* compressedFixture)
{
    gsply::GaussianPlyDecoder decoder;
    gs::GaussianCloudData reference;
    gs::GaussianCloudData compressed;
    std::string error;
    gsply::GaussianPlyDialect dialect = gsply::GaussianPlyDialect::Graphdeco;
    CHECK(decoder.Decode(Fixture(plyFixture), &reference, nullptr, &error));
    CHECK(decoder.Decode(Fixture(compressedFixture), &compressed, nullptr,
                         &error, nullptr, &dialect));
    CHECK(dialect == gsply::GaussianPlyDialect::Compressed);
    if (!error.empty()) {
        std::cerr << compressedFixture << ": " << error << "\n";
    }
    CheckContract(reference, plyFixture);
    CheckContract(compressed, compressedFixture);
    if (!ShapesAgree(reference, compressed)) {
        return;
    }

    const std::vector<Chunk> chunks = ReadChunks(Fixture(compressedFixture));
    const std::size_t count = reference.gaussianCount;
    CHECK(chunks.size() == (count + 255) / 256);
    if (chunks.size() != (count + 255) / 256) {
        return;
    }

    const double steps[3] = {4094.0, 2046.0, 4094.0};
    for (std::size_t i = 0; i < count; ++i) {
        const Chunk& b = chunks[i / 256];
        const float rp[3] = {reference.positions[i].x, reference.positions[i].y,
                             reference.positions[i].z};
        const float cp[3] = {compressed.positions[i].x,
                             compressed.positions[i].y,
                             compressed.positions[i].z};
        const float rs[3] = {reference.scales[i].x, reference.scales[i].y,
                             reference.scales[i].z};
        const float cs[3] = {compressed.scales[i].x, compressed.scales[i].y,
                             compressed.scales[i].z};
        const float rd[3] = {reference.dcCoefficients[i].x,
                             reference.dcCoefficients[i].y,
                             reference.dcCoefficients[i].z};
        const float cd[3] = {compressed.dcCoefficients[i].x,
                             compressed.dcCoefficients[i].y,
                             compressed.dcCoefficients[i].z};
        for (int a = 0; a < 3; ++a) {
            // Half a code step plus float32 rounding of bounds and result.
            const float position = static_cast<float>(
                (b[3 + a] - b[a]) / steps[a] +
                3.0e-7 * (std::fabs(b[a]) + std::fabs(b[3 + a]) + 1.0));
            CheckClose(rp[a], cp[a], position, "position", i);
            const float logScale =
                static_cast<float>((b[9 + a] - b[6 + a]) / steps[a] + 2.0e-6);
            CheckClose(std::log(rs[a]), std::log(cs[a]), logScale, "log scale",
                       i);
            const float dc = static_cast<float>(
                (b[15 + a] - b[12 + a]) / (510.0 * kShC0) + 1.0e-5);
            CheckClose(rd[a], cd[a], dc, "dc", i);
        }
        CheckClose(reference.opacities[i], compressed.opacities[i],
                   1.0f / 510.0f + 1.0e-6f, "opacity", i);

        const gs::Quaternion& q = reference.rotations[i];
        const gs::Quaternion r = AlignSign(q, compressed.rotations[i]);
        CheckClose(q.real, r.real, kRotation, "rotation.real", i);
        CheckClose(q.i, r.i, kRotation, "rotation.i", i);
        CheckClose(q.j, r.j, kRotation, "rotation.j", i);
        CheckClose(q.k, r.k, kRotation, "rotation.k", i);
    }
    for (std::size_t k = 0; k < reference.restCoefficients.size(); ++k) {
        const gs::Float3& r = reference.restCoefficients[k];
        const gs::Float3& c = compressed.restCoefficients[k];
        CheckClose(r.x, c.x, kShHalfStep, "rest.r", k);
        CheckClose(r.y, c.y, kShHalfStep, "rest.g", k);
        CheckClose(r.z, c.z, kShHalfStep, "rest.b", k);
    }
}

} // namespace

int main()
{
    // Degree 3, four Gaussians, one chunk: all 15 band signs and the
    // channel-major transpose against the PLY path.
    ComparePair("equiv-degree3-exact-binary-le.ply",
                "equiv-degree3-exact.compressed.ply");
    // Degree 1, off-grid values including a position near 1000.
    ComparePair("equiv-degree1-offgrid-binary-le.ply",
                "equiv-degree1-offgrid.compressed.ply");
    // Degree 2, 600 Gaussians over three chunks, the last partial.
    ComparePair("equiv-degree2-chunked-binary-le.ply",
                "equiv-degree2-chunked.compressed.ply");
    return Report("PLY/compressed-PLY equivalence");
}
