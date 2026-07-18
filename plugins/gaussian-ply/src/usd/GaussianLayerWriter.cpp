// SPDX-License-Identifier: Apache-2.0
#include "usd/GaussianLayerWriter.h"

#include "openstrata/gs/GaussianMath.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/kind/registry.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdVol/particleField3DGaussianSplat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace openstrata::gs::usd {
namespace {

PXR_NS::GfVec3f ToGf(const Float3& value)
{
    return {value.x, value.y, value.z};
}

} // namespace

bool GaussianLayerWriter::WriteToString(
    const GaussianCloudData& cloud,
    const std::string& sourceFormat,
    std::string* outUsda,
    std::string* error) const
{
    if (!outUsda) {
        if (error) *error = "Gaussian writer received a null string output.";
        return false;
    }
    std::string validationError;
    if (!ValidateGaussianCloud(cloud, &validationError)) {
        if (error) *error = validationError;
        return false;
    }

    PXR_NS::SdfLayerRefPtr layer = PXR_NS::SdfLayer::CreateAnonymous(".usda");
    PXR_NS::UsdStageRefPtr stage = PXR_NS::UsdStage::Open(layer);
    if (!stage) {
        if (error) *error = "Could not create an in-memory USD stage.";
        return false;
    }

    PXR_NS::UsdGeomSetStageUpAxis(stage, PXR_NS::UsdGeomTokens->y);
    PXR_NS::UsdGeomSetStageMetersPerUnit(stage, 1.0);

    const PXR_NS::SdfPath assetPath("/Asset");
    const PXR_NS::SdfPath splatPath("/Asset/Splat");
    PXR_NS::UsdPrim asset =
        PXR_NS::UsdGeomXform::Define(stage, assetPath).GetPrim();
    if (!asset) {
        if (error) *error = "Could not define /Asset.";
        return false;
    }
    PXR_NS::UsdModelAPI(asset).SetKind(PXR_NS::KindTokens->component);
    stage->SetDefaultPrim(asset);
    asset.SetCustomDataByKey(
        PXR_NS::TfToken("gs:sourceFormat"), PXR_NS::VtValue(sourceFormat));
    asset.SetCustomDataByKey(
        PXR_NS::TfToken("gs:gaussianCount"),
        PXR_NS::VtValue(static_cast<std::uint64_t>(cloud.gaussianCount)));
    asset.SetCustomDataByKey(
        PXR_NS::TfToken("gs:shDegree"), PXR_NS::VtValue(cloud.shDegree));

    PXR_NS::UsdVolParticleField3DGaussianSplat splat =
        PXR_NS::UsdVolParticleField3DGaussianSplat::Define(stage, splatPath);
    if (!splat) {
        if (error) *error = "Could not define /Asset/Splat as a Gaussian particle field.";
        return false;
    }

    PXR_NS::VtArray<PXR_NS::GfVec3f> positions;
    PXR_NS::VtArray<PXR_NS::GfVec3f> scales;
    PXR_NS::VtArray<PXR_NS::GfQuatf> rotations;
    PXR_NS::VtArray<float> opacities;
    positions.reserve(cloud.gaussianCount);
    scales.reserve(cloud.gaussianCount);
    rotations.reserve(cloud.gaussianCount);
    opacities.reserve(cloud.gaussianCount);
    for (std::size_t i = 0; i < cloud.gaussianCount; ++i) {
        positions.push_back(ToGf(cloud.positions[i]));
        scales.push_back(ToGf(cloud.scales[i]));
        rotations.emplace_back(
            cloud.rotations[i].real,
            PXR_NS::GfVec3f(
                cloud.rotations[i].i,
                cloud.rotations[i].j,
                cloud.rotations[i].k));
        opacities.push_back(cloud.opacities[i]);
    }

    if (!splat.CreatePositionsAttr().Set(positions) ||
        !splat.CreateScalesAttr().Set(scales) ||
        !splat.CreateOrientationsAttr().Set(rotations) ||
        !splat.CreateOpacitiesAttr().Set(opacities) ||
        !splat.CreateRadianceSphericalHarmonicsDegreeAttr().Set(cloud.shDegree)) {
        if (error) *error = "Could not author a required Gaussian attribute.";
        return false;
    }

    const std::size_t coefficientsPerGaussian =
        cloud.CoefficientsPerGaussian();
    const std::size_t restPerGaussian = coefficientsPerGaussian - 1;
    PXR_NS::VtArray<PXR_NS::GfVec3f> coefficients;
    coefficients.reserve(cloud.gaussianCount * coefficientsPerGaussian);
    for (std::size_t gaussian = 0;
         gaussian < cloud.gaussianCount;
         ++gaussian) {
        coefficients.push_back(ToGf(cloud.dcCoefficients[gaussian]));
        const std::size_t base = gaussian * restPerGaussian;
        for (std::size_t coefficient = 0;
             coefficient < restPerGaussian;
             ++coefficient) {
            coefficients.push_back(
                ToGf(cloud.restCoefficients[base + coefficient]));
        }
    }
    if (!splat.CreateRadianceSphericalHarmonicsCoefficientsAttr().Set(
            coefficients)) {
        if (error) *error = "Could not author Gaussian SH coefficients.";
        return false;
    }

    // A conservative three-sigma bound. Rotation can only reduce an axis from
    // this max-scale sphere, so this remains valid for every ellipsoid.
    PXR_NS::GfVec3f minimum(std::numeric_limits<float>::max());
    PXR_NS::GfVec3f maximum(-std::numeric_limits<float>::max());
    for (std::size_t i = 0; i < cloud.gaussianCount; ++i) {
        const Float3& p = cloud.positions[i];
        const Float3& s = cloud.scales[i];
        const double radius = 3.0 * static_cast<double>(
            std::max({s.x, s.y, s.z}));
        if (!std::isfinite(radius) ||
            radius > std::numeric_limits<float>::max()) {
            if (error) *error = "Gaussian extent exceeds float range.";
            return false;
        }
        const float r = static_cast<float>(radius);
        minimum[0] = std::min(minimum[0], p.x - r);
        minimum[1] = std::min(minimum[1], p.y - r);
        minimum[2] = std::min(minimum[2], p.z - r);
        maximum[0] = std::max(maximum[0], p.x + r);
        maximum[1] = std::max(maximum[1], p.y + r);
        maximum[2] = std::max(maximum[2], p.z + r);
    }
    PXR_NS::VtArray<PXR_NS::GfVec3f> extent = {minimum, maximum};
    if (!splat.CreateExtentAttr().Set(extent)) {
        if (error) *error = "Could not author Gaussian extent.";
        return false;
    }

    if (!stage->ExportToString(outUsda)) {
        if (error) *error = "Could not serialize the generated USD stage.";
        return false;
    }
    return true;
}

} // namespace openstrata::gs::usd
