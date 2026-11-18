// SPDX-License-Identifier: Apache-2.0
//
// Cross-format equivalence, PLY against `.splat`: the shared source models,
// encoded by the bundle's reference-writer encoder
// (plugins/gaussian-splat/tools/generate_fixtures.py), must decode to the same
// GaussianCloudData as the lossless PLY encoding, apart from what `.splat`
// cannot store. The CC0 corpus conversions are checked the same way against
// the committed PLY subsets they were made from.
//
// Both formats store the Graphdeco RDF frame and both decoders apply the
// shared FlipYZAxes, so positions agree to the bit and this pins the `.splat`
// rotation byte order and sign handling against the PLY path. `.splat` has no
// rest coefficients: the decoded degree must be 0 and only DC is compared.
//
// Tolerances (EQUIVALENCE.md §2): float32 positions and linear scales are
// exact up to PLY's log/exp round trip; DC and opacity are one 8-bit step,
// because the reference writer truncates; each rotation component is held to
// 0.025, the bound for a truncated quaternion after renormalization
// (SPLAT_MAPPING.md §4). DC outside the range a byte can hold is clipped by
// the writer, so the expected DC is clipped first (SPLAT_MAPPING.md §3).

#include "equivalence_common.h"

#include "io/GaussianPlyDecoder.h"
#include "io/GaussianSplatDecoder.h"
#include "openstrata/gs/GaussianCloudData.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

namespace gs = openstrata::gs;
namespace gsply = openstrata::gs::ply;
namespace gssplat = openstrata::gs::splat;
using namespace openstrata::gs::equivalence;

namespace {

constexpr float kShC0 = 0.28209479177387814f;
constexpr float kRotation = 0.025f;
constexpr float kOpacity = 1.0f / 255.0f + 1.0e-6f;
constexpr float kDc = 1.0f / (255.0f * kShC0) + 1.0e-5f;
// The DC range a colour byte can hold, +-0.5 / SH_C0.
constexpr float kDcLimit = 0.5f / kShC0;

std::string RepoPath(const char* relative)
{
    return (std::filesystem::path(EQUIVALENCE_REPO_DIR) / relative).string();
}

void ComparePair(const std::string& plyPath, const std::string& splatPath,
                 const char* label)
{
    gs::GaussianCloudData reference;
    gs::GaussianCloudData splat;
    std::string error;
    CHECK(gsply::GaussianPlyDecoder().Decode(plyPath, &reference, nullptr,
                                             &error));
    if (!error.empty()) {
        std::cerr << plyPath << ": " << error << "\n";
    }
    CHECK(gssplat::GaussianSplatDecoder().Decode(splatPath, &splat, nullptr,
                                                 &error));
    if (!error.empty()) {
        std::cerr << splatPath << ": " << error << "\n";
    }
    CheckContract(reference, label);
    CheckContract(splat, label);

    CHECK(splat.shDegree == 0);
    CHECK(splat.restCoefficients.empty());
    CHECK(reference.gaussianCount == splat.gaussianCount);
    if (reference.gaussianCount != splat.gaussianCount ||
        splat.positions.size() != splat.gaussianCount) {
        std::cerr << label << ": counts differ\n";
        return;
    }

    std::size_t clipped = 0;
    for (std::size_t i = 0; i < reference.gaussianCount; ++i) {
        const gs::Float3& rp = reference.positions[i];
        const gs::Float3& sp = splat.positions[i];
        CheckClose(rp.x, sp.x, 0.0f, "position.x", i);
        CheckClose(rp.y, sp.y, 0.0f, "position.y", i);
        CheckClose(rp.z, sp.z, 0.0f, "position.z", i);

        const gs::Float3& rs = reference.scales[i];
        const gs::Float3& ss = splat.scales[i];
        CheckRelative(rs.x, ss.x, 2.0e-6f, "scale.x", i);
        CheckRelative(rs.y, ss.y, 2.0e-6f, "scale.y", i);
        CheckRelative(rs.z, ss.z, 2.0e-6f, "scale.z", i);

        CheckClose(reference.opacities[i], splat.opacities[i], kOpacity,
                   "opacity", i);

        const float rd[3] = {reference.dcCoefficients[i].x,
                             reference.dcCoefficients[i].y,
                             reference.dcCoefficients[i].z};
        const float sd[3] = {splat.dcCoefficients[i].x,
                             splat.dcCoefficients[i].y,
                             splat.dcCoefficients[i].z};
        bool anyClipped = false;
        for (int c = 0; c < 3; ++c) {
            const float expected = std::clamp(rd[c], -kDcLimit, kDcLimit);
            anyClipped = anyClipped || expected != rd[c];
            CheckClose(expected, sd[c], kDc, "dc", i);
        }
        clipped += anyClipped ? 1 : 0;

        const gs::Quaternion& q = reference.rotations[i];
        const gs::Quaternion r = AlignSign(q, splat.rotations[i]);
        CheckClose(q.real, r.real, kRotation, "rotation.real", i);
        CheckClose(q.i, r.i, kRotation, "rotation.i", i);
        CheckClose(q.j, r.j, kRotation, "rotation.j", i);
        CheckClose(q.k, r.k, kRotation, "rotation.k", i);
    }
    std::cout << label << ": " << reference.gaussianCount << " Gaussians, "
              << clipped << " with clipped DC\n";
}

void CompareFixtures(const char* plyFixture, const char* splatFixture)
{
    ComparePair(Fixture(plyFixture), Fixture(splatFixture), splatFixture);
}

void CompareCorpus(const char* scene)
{
    const std::string base = std::string(scene) + "/" + scene + "-top8192";
    const std::string ply = RepoPath(
        ("plugins/gaussian-ply/tests/corpus/" + base + ".ply").c_str());
    const std::string splat = RepoPath(
        ("plugins/gaussian-splat/tests/corpus/" + base + ".splat").c_str());
    ComparePair(ply, splat, scene);
}

} // namespace

int main()
{
    // Degree 3 on the SPZ grid; the rest coefficients are dropped.
    CompareFixtures("equiv-degree3-exact-binary-le.ply",
                    "equiv-degree3-exact.splat");
    // Degree 1, off-grid values including a position near 1000 and an
    // opacity below two alpha steps.
    CompareFixtures("equiv-degree1-offgrid-binary-le.ply",
                    "equiv-degree1-offgrid.splat");
    // Degree 2, 600 Gaussians with random rotations.
    CompareFixtures("equiv-degree2-chunked-binary-le.ply",
                    "equiv-degree2-chunked.splat");
    // Real captures: the CC0 subsets and their `.splat` conversions. About a
    // quarter of their Gaussians carry DC outside the storable range
    // (format-spike-splat-2026-11-19.md §6).
    CompareCorpus("yashica-t4");
    CompareCorpus("leica-sofort");
    return Report("PLY/.splat equivalence");
}
