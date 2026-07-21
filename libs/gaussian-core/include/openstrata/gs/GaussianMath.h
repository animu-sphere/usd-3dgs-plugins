// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"

#include <cstddef>
#include <string>

namespace openstrata::gs {

float Sigmoid(float value) noexcept;

bool DecodeLogScale(const Float3& stored, Float3* actual) noexcept;

// Returns false only for non-finite input. A near-zero quaternion is replaced
// with identity and reported through replacedWithIdentity.
bool NormalizeQuaternion(
    const Quaternion& stored,
    Quaternion* normalized,
    bool* replacedWithIdentity = nullptr,
    bool* changed = nullptr) noexcept;

// coefficientCount includes the DC term. Valid layouts are 1, 4, 9, 16, ...
bool InferShDegree(std::size_t coefficientCount, int* degree) noexcept;

// Negates the Y and Z axes of every position, rotation, and rest SH
// coefficient in place: the RDF <-> RUB frame conversion (an involution)
// decided by ADR 0001. Scales are per-axis standard deviations along the
// Gaussian's own principal axes and the DC term is isotropic, so neither
// changes. The cloud must already have consistent array lengths; call it
// from a decoder after arrays are built, never from the writer.
void FlipYZAxes(GaussianCloudData* cloud) noexcept;

// The authored-extent policy (design policy §6): a conservative per-Gaussian
// three-sigma bound over the largest scale axis. Rotation can only reduce an
// axis from that max-scale sphere, so the bound holds for every ellipsoid.
// This is the single implementation the writer authors and the decoder test
// kit compares against, so a stage need not be authored to know the extent a
// cloud will produce. Returns false when count is zero or a bound leaves
// float range; the outputs are untouched on failure.
bool ComputeCloudExtent(
    const Float3* positions,
    const Float3* scales,
    std::size_t count,
    Float3* outMinimum,
    Float3* outMaximum) noexcept;

bool ValidateGaussianCloud(
    const GaussianCloudData& cloud,
    std::string* error = nullptr) noexcept;

} // namespace openstrata::gs
