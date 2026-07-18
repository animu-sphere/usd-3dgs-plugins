// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <vector>

namespace openstrata::gs {

struct Float3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Scalar-first quaternion. This matches the common 3DGS PLY rot_0..rot_3
// convention (w, x, y, z) without importing a USD math type into the model.
struct Quaternion {
    float real = 1.0f;
    float i = 0.0f;
    float j = 0.0f;
    float k = 0.0f;
};

struct GaussianCloudData {
    std::vector<Float3> positions;
    std::vector<Float3> scales;
    std::vector<Quaternion> rotations;
    std::vector<float> opacities;

    // DC and non-DC terms remain separate in the canonical model. Rest terms
    // are RGB triples ordered by Gaussian, then SH coefficient.
    std::vector<Float3> dcCoefficients;
    std::vector<Float3> restCoefficients;

    int shDegree = 0;
    std::size_t gaussianCount = 0;

    std::size_t CoefficientsPerGaussian() const noexcept
    {
        const std::size_t side = static_cast<std::size_t>(shDegree + 1);
        return side * side;
    }
};

} // namespace openstrata::gs
