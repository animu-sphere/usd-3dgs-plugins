// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"

#include <string>

namespace openstrata::gs::usd {

// Authors the format-independent cloud into OpenUSD's standard
// ParticleField3DGaussianSplat schema and serializes it as USDA.
class GaussianLayerWriter {
public:
    bool WriteToString(
        const GaussianCloudData& cloud,
        const std::string& sourceFormat,
        std::string* outUsda,
        std::string* error = nullptr) const;
};

} // namespace openstrata::gs::usd
