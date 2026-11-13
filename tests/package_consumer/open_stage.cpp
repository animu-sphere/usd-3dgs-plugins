// SPDX-License-Identifier: Apache-2.0
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdVol/particleField3DGaussianSplat.h>

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: open_stage <asset> <gaussian-count>\n";
        return 2;
    }

    const auto stage = PXR_NS::UsdStage::Open(argv[1]);
    if (!stage || !stage->GetDefaultPrim()) {
        std::cerr << "Cannot open the stage or find its default prim: "
                  << argv[1] << '\n';
        return 1;
    }

    const auto splat = PXR_NS::UsdVolParticleField3DGaussianSplat::Get(
        stage, PXR_NS::SdfPath("/Asset/Splat"));
    PXR_NS::VtVec3fArray positions;
    if (!splat || !splat.GetPositionsAttr().Get(&positions) ||
        positions.size() != std::stoul(argv[2])) {
        std::cerr << "Missing or invalid Gaussian positions: " << argv[1]
                  << '\n';
        return 1;
    }
    return 0;
}
