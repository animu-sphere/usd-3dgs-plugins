// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared scaffolding for the cross-format equivalence executables.
//
// There is one executable per quantizing format rather than a single binary
// comparing all three, because `gaussianSpzDecoder` and `gaussianSogDecoder`
// each compile their own vendored `miniz.c` translation unit — with different
// definitions, since SPZ compiles the archive APIs out and SOG needs them for
// the bundled `.sog` ZIP. Linking both static libraries into one program is a
// duplicate-symbol error, and neither bundle may `#include` the other's code
// (API_BOUNDARY.md rule 3). Splitting the executables costs nothing in
// coverage: every comparison is against the lossless PLY side anyway, so
// "SPZ agrees with PLY" and "SOG agrees with PLY" together are exactly the
// three-way agreement the criterion asks for.

#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/testing/CloudContract.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace openstrata::gs::equivalence {

inline int failures = 0;

#define CHECK(expr)                                                      \
    do {                                                                 \
        if (!(expr)) {                                                   \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #expr "\n"; \
            ++openstrata::gs::equivalence::failures;                     \
        }                                                                \
    } while (false)

inline std::string Fixture(const char* name)
{
    return (std::filesystem::path(EQUIVALENCE_FIXTURE_DIR) / name).string();
}

inline void CheckClose(
    float reference,
    float other,
    float tolerance,
    const char* what,
    std::size_t index)
{
    if (!(std::fabs(reference - other) <= tolerance)) {
        std::cerr << what << '[' << index << "]: ply " << reference << " vs "
                  << other << " (delta " << std::fabs(reference - other)
                  << " > tolerance " << tolerance << ")\n";
        ++failures;
    }
}

inline void CheckRelative(
    float reference,
    float other,
    float tolerance,
    const char* what,
    std::size_t index)
{
    const float bound = tolerance * std::fabs(reference);
    if (!(std::fabs(reference - other) <= bound)) {
        std::cerr << what << '[' << index << "]: ply " << reference << " vs "
                  << other << " (relative "
                  << std::fabs(reference - other) / std::fabs(reference)
                  << " > " << tolerance << ")\n";
        ++failures;
    }
}

inline void CheckContract(const GaussianCloudData& cloud, const char* which)
{
    for (const std::string& violation : testing::CheckCloudContract(cloud)) {
        std::cerr << which << " contract: " << violation << "\n";
        ++failures;
    }
}

// q and -q are the same rotation, and both quantizing formats force their
// stored-largest component non-negative while PLY keeps whatever sign the
// source used. Aligning the double cover before comparing keeps a legitimate
// sign difference from reading as disagreement.
inline Quaternion AlignSign(const Quaternion& reference, Quaternion other)
{
    const float dot = reference.real * other.real + reference.i * other.i +
        reference.j * other.j + reference.k * other.k;
    if (dot < 0.0f) {
        other.real = -other.real;
        other.i = -other.i;
        other.j = -other.j;
        other.k = -other.k;
    }
    return other;
}

// Structural agreement, checked before any per-element comparison so a pair
// whose sizes differ reports that instead of thousands of value mismatches.
inline bool ShapesAgree(
    const GaussianCloudData& reference, const GaussianCloudData& other)
{
    const int before = failures;
    CHECK(reference.gaussianCount == other.gaussianCount);
    CHECK(reference.shDegree == other.shDegree);
    CHECK(reference.positions.size() == other.positions.size());
    CHECK(reference.scales.size() == other.scales.size());
    CHECK(reference.rotations.size() == other.rotations.size());
    CHECK(reference.opacities.size() == other.opacities.size());
    CHECK(reference.dcCoefficients.size() == other.dcCoefficients.size());
    CHECK(reference.restCoefficients.size() == other.restCoefficients.size());
    return failures == before;
}

inline int Report(const char* suite)
{
    if (failures != 0) {
        std::cerr << failures << ' ' << suite << " check(s) failed\n";
        return 1;
    }
    std::cout << "all " << suite << " checks passed\n";
    return 0;
}

} // namespace openstrata::gs::equivalence
