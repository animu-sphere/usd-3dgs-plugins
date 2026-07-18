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
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace openstrata::gs::usd {
namespace {

static_assert(sizeof(Float3) == sizeof(PXR_NS::GfVec3f) &&
        std::is_trivially_copyable_v<Float3>,
    "Float3 must be byte-compatible with GfVec3f for bulk copies");

PXR_NS::GfVec3f ToGf(const Float3& value)
{
    return {value.x, value.y, value.z};
}

PXR_NS::VtArray<PXR_NS::GfVec3f> TakeVec3fArray(std::vector<Float3>* source)
{
    PXR_NS::VtArray<PXR_NS::GfVec3f> array(source->size());
    if (!source->empty()) {
        std::memcpy(
            array.data(), source->data(),
            source->size() * sizeof(PXR_NS::GfVec3f));
    }
    std::vector<Float3>().swap(*source);
    return array;
}

} // namespace

bool GaussianLayerWriter::WriteToLayer(
    GaussianCloudData&& cloud,
    const std::string& sourceFormat,
    PXR_NS::SdfLayerRefPtr* outLayer,
    std::string* error) const
{
    if (!outLayer) {
        if (error) *error = "Gaussian writer received a null layer output.";
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

    // Attribute values share COW storage with the layer after Set, so keeping
    // the locals alive for the extent pass below costs no extra copies.
    PXR_NS::VtArray<PXR_NS::GfVec3f> positions =
        TakeVec3fArray(&cloud.positions);
    PXR_NS::VtArray<PXR_NS::GfVec3f> scales = TakeVec3fArray(&cloud.scales);

    PXR_NS::VtArray<PXR_NS::GfQuatf> rotations(cloud.gaussianCount);
    PXR_NS::GfQuatf* rotationOut = rotations.data();
    for (std::size_t i = 0; i < cloud.gaussianCount; ++i) {
        const Quaternion& q = cloud.rotations[i];
        rotationOut[i] = PXR_NS::GfQuatf(q.real, q.i, q.j, q.k);
    }
    std::vector<Quaternion>().swap(cloud.rotations);

    PXR_NS::VtArray<float> opacities(cloud.gaussianCount);
    std::memcpy(
        opacities.data(), cloud.opacities.data(),
        cloud.gaussianCount * sizeof(float));
    std::vector<float>().swap(cloud.opacities);

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
    PXR_NS::VtArray<PXR_NS::GfVec3f> coefficients(
        cloud.gaussianCount * coefficientsPerGaussian);
    PXR_NS::GfVec3f* coefficientOut = coefficients.data();
    for (std::size_t gaussian = 0;
         gaussian < cloud.gaussianCount;
         ++gaussian) {
        PXR_NS::GfVec3f* out = coefficientOut +
            gaussian * coefficientsPerGaussian;
        out[0] = ToGf(cloud.dcCoefficients[gaussian]);
        const std::size_t base = gaussian * restPerGaussian;
        for (std::size_t coefficient = 0;
             coefficient < restPerGaussian;
             ++coefficient) {
            out[1 + coefficient] = ToGf(cloud.restCoefficients[base + coefficient]);
        }
    }
    std::vector<Float3>().swap(cloud.dcCoefficients);
    std::vector<Float3>().swap(cloud.restCoefficients);
    if (!splat.CreateRadianceSphericalHarmonicsCoefficientsAttr().Set(
            coefficients)) {
        if (error) *error = "Could not author Gaussian SH coefficients.";
        return false;
    }

    // A conservative three-sigma bound. Rotation can only reduce an axis from
    // this max-scale sphere, so this remains valid for every ellipsoid.
    const PXR_NS::GfVec3f* positionIn = positions.cdata();
    const PXR_NS::GfVec3f* scaleIn = scales.cdata();
    PXR_NS::GfVec3f minimum(std::numeric_limits<float>::max());
    PXR_NS::GfVec3f maximum(-std::numeric_limits<float>::max());
    for (std::size_t i = 0; i < cloud.gaussianCount; ++i) {
        const PXR_NS::GfVec3f& p = positionIn[i];
        const PXR_NS::GfVec3f& s = scaleIn[i];
        const double radius = 3.0 * static_cast<double>(
            std::max({s[0], s[1], s[2]}));
        if (!std::isfinite(radius) ||
            radius > std::numeric_limits<float>::max()) {
            if (error) *error = "Gaussian extent exceeds float range.";
            return false;
        }
        const float r = static_cast<float>(radius);
        minimum[0] = std::min(minimum[0], p[0] - r);
        minimum[1] = std::min(minimum[1], p[1] - r);
        minimum[2] = std::min(minimum[2], p[2] - r);
        maximum[0] = std::max(maximum[0], p[0] + r);
        maximum[1] = std::max(maximum[1], p[1] + r);
        maximum[2] = std::max(maximum[2], p[2] + r);
    }
    PXR_NS::VtArray<PXR_NS::GfVec3f> extent = {minimum, maximum};
    if (!splat.CreateExtentAttr().Set(extent)) {
        if (error) *error = "Could not author Gaussian extent.";
        return false;
    }

    *outLayer = layer;
    return true;
}

} // namespace openstrata::gs::usd
