// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"

#include "pxr/usd/sdf/layer.h"

#include <string>

namespace openstrata::gs::usd {

// Authors the format-independent cloud into OpenUSD's standard
// ParticleField3DGaussianSplat schema on an anonymous layer. The cloud is
// consumed: each array is released once it has been copied into its authored
// attribute so the cloud and the authored arrays never coexist in full.
class GaussianLayerWriter {
public:
    bool WriteToLayer(
        GaussianCloudData&& cloud,
        const std::string& sourceFormat,
        PXR_NS::SdfLayerRefPtr* outLayer,
        std::string* error = nullptr) const;
};

} // namespace openstrata::gs::usd
