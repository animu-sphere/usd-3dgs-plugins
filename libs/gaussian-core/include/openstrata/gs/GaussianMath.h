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

bool ValidateGaussianCloud(
    const GaussianCloudData& cloud,
    std::string* error = nullptr) noexcept;

} // namespace openstrata::gs
