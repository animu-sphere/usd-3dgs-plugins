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

bool ValidateGaussianCloud(
    const GaussianCloudData& cloud,
    std::string* error = nullptr) noexcept;

} // namespace openstrata::gs
