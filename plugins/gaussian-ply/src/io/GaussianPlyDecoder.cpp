// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianPlyDecoder.h"

#include "io/PlyReader.h"
#include "openstrata/gs/GaussianMath.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace openstrata::gs::ply {
namespace {

const std::vector<std::string> RequiredProperties = {
    "x", "y", "z",
    "scale_0", "scale_1", "scale_2",
    "rot_0", "rot_1", "rot_2", "rot_3",
    "opacity",
    "f_dc_0", "f_dc_1", "f_dc_2",
};

std::map<std::string, const PlyPropertyDescription*> PropertyMap(
    const PlyHeader& header)
{
    std::map<std::string, const PlyPropertyDescription*> result;
    for (const PlyPropertyDescription& property : header.vertexProperties) {
        result.emplace(property.name, &property);
    }
    return result;
}

bool HasGaussianSignature(
    const std::map<std::string, const PlyPropertyDescription*>& properties)
{
    for (const char* name : {"scale_0", "rot_0", "opacity", "f_dc_0"}) {
        if (properties.find(name) == properties.end()) {
            return false;
        }
    }
    return true;
}

bool ParseRestIndex(const std::string& name, std::size_t* index)
{
    static const std::string prefix = "f_rest_";
    if (name.compare(0, prefix.size(), prefix) != 0 || !index) {
        return false;
    }
    const std::string suffix = name.substr(prefix.size());
    if (suffix.empty() ||
        !std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        return false;
    }
    try {
        const unsigned long long parsed = std::stoull(suffix);
        if (parsed > std::numeric_limits<std::size_t>::max()) {
            return false;
        }
        *index = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool AllFinite(const std::vector<float>& column) noexcept
{
    for (const float value : column) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

void AddCountWarning(
    std::vector<std::string>* warnings,
    std::size_t count,
    const char* singular,
    const char* plural)
{
    if (!warnings || count == 0) {
        return;
    }
    warnings->push_back(std::to_string(count) + " " +
        (count == 1 ? singular : plural));
}

} // namespace

bool GaussianPlyDecoder::CanRead(const std::string& path) const noexcept
{
    try {
        PlyHeader header;
        if (!PlyReader().ReadHeader(path, &header)) {
            return false;
        }
        return HasGaussianSignature(PropertyMap(header));
    } catch (...) {
        return false;
    }
}

bool GaussianPlyDecoder::Decode(
    const std::string& path,
    GaussianCloudData* cloud,
    std::vector<std::string>* warnings,
    std::string* error) const
{
    if (!cloud) {
        if (error) *error = "Gaussian decoder received a null cloud output.";
        return false;
    }

    PlyReader reader;
    PlyHeader header;
    if (!reader.ReadHeader(path, &header, error)) {
        return false;
    }
    const auto properties = PropertyMap(header);
    const bool hasAnyGaussianProperty =
        properties.find("scale_0") != properties.end() ||
        properties.find("rot_0") != properties.end() ||
        properties.find("opacity") != properties.end() ||
        properties.find("f_dc_0") != properties.end();
    if (!HasGaussianSignature(properties) && !hasAnyGaussianProperty) {
        if (error) {
            *error = "The file is a valid PLY file, but it is not a supported "
                "Gaussian Splatting PLY dialect.";
        }
        return false;
    }
    if (header.vertexCount == 0) {
        if (error) *error = "Gaussian PLY vertex element contains no particles.";
        return false;
    }

    for (const std::string& name : RequiredProperties) {
        const auto found = properties.find(name);
        if (found == properties.end()) {
            if (error) *error = "Gaussian PLY is missing required property '" + name + "'.";
            return false;
        }
        if (found->second->isList || found->second->type == PlyScalarType::Invalid) {
            if (error) *error = "Gaussian PLY property '" + name +
                "' is not a supported scalar type.";
            return false;
        }
    }

    std::map<std::size_t, std::string> indexedRest;
    for (const PlyPropertyDescription& property : header.vertexProperties) {
        if (property.name.rfind("f_rest_", 0) != 0) {
            continue;
        }
        std::size_t index = 0;
        if (!ParseRestIndex(property.name, &index)) {
            if (error) *error = "Gaussian PLY has invalid SH property '" +
                property.name + "'.";
            return false;
        }
        if (property.isList || property.type == PlyScalarType::Invalid) {
            if (error) *error = "Gaussian PLY SH property '" + property.name +
                "' is not a supported scalar type.";
            return false;
        }
        if (!indexedRest.emplace(index, property.name).second) {
            if (error) *error = "Gaussian PLY has duplicate SH property index " +
                std::to_string(index) + ".";
            return false;
        }
    }
    for (std::size_t i = 0; i < indexedRest.size(); ++i) {
        if (indexedRest.find(i) == indexedRest.end()) {
            if (error) *error = "Gaussian PLY f_rest_* properties are not contiguous.";
            return false;
        }
    }
    if (indexedRest.size() % 3 != 0) {
        if (error) *error = "Gaussian PLY SH property count is not divisible by RGB.";
        return false;
    }

    const std::size_t restPerChannel = indexedRest.size() / 3;
    const std::size_t coefficientsPerGaussian = restPerChannel + 1;
    int shDegree = 0;
    if (!InferShDegree(coefficientsPerGaussian, &shDegree)) {
        if (error) *error = "Gaussian PLY SH property count does not form a valid degree.";
        return false;
    }

    std::vector<std::string> requested = RequiredProperties;
    for (const auto& entry : indexedRest) {
        requested.push_back(entry.second);
    }

    PlyDocument document;
    if (!reader.Read(path, requested, &document, error)) {
        return false;
    }

    const std::size_t count = header.vertexCount;
    // Columns are moved out of the document and freed as they are consumed so
    // the decoded cloud and the parsed columns never coexist in full.
    auto takeColumn = [&document, count, error](
        const std::string& name, std::vector<float>* column) {
        const auto found = document.vertexProperties.find(name);
        if (found == document.vertexProperties.end() ||
            found->second.size() != count) {
            if (error) {
                *error = "Gaussian PLY property '" + name +
                    "' is missing or does not match the vertex count.";
            }
            return false;
        }
        *column = std::move(found->second);
        document.vertexProperties.erase(found);
        return true;
    };

    GaussianCloudData result;
    result.gaussianCount = count;
    result.shDegree = shDegree;

    {
        std::vector<float> x;
        std::vector<float> y;
        std::vector<float> z;
        if (!takeColumn("x", &x) ||
            !takeColumn("y", &y) ||
            !takeColumn("z", &z)) {
            return false;
        }
        if (!AllFinite(x) || !AllFinite(y) || !AllFinite(z)) {
            if (error) *error = "Gaussian PLY contains a non-finite or out-of-range value.";
            return false;
        }
        result.positions.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            result.positions[row] = {x[row], y[row], z[row]};
        }
    }

    {
        std::vector<float> scale0;
        std::vector<float> scale1;
        std::vector<float> scale2;
        if (!takeColumn("scale_0", &scale0) ||
            !takeColumn("scale_1", &scale1) ||
            !takeColumn("scale_2", &scale2)) {
            return false;
        }
        if (!AllFinite(scale0) || !AllFinite(scale1) || !AllFinite(scale2)) {
            if (error) *error = "Gaussian PLY contains a non-finite or out-of-range value.";
            return false;
        }
        result.scales.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            if (!DecodeLogScale(
                    {scale0[row], scale1[row], scale2[row]},
                    &result.scales[row])) {
                if (error) *error = "Gaussian PLY scale cannot be converted from log space.";
                return false;
            }
        }
    }

    std::size_t normalizedQuaternions = 0;
    std::size_t identityQuaternions = 0;
    {
        std::vector<float> rot0;
        std::vector<float> rot1;
        std::vector<float> rot2;
        std::vector<float> rot3;
        if (!takeColumn("rot_0", &rot0) ||
            !takeColumn("rot_1", &rot1) ||
            !takeColumn("rot_2", &rot2) ||
            !takeColumn("rot_3", &rot3)) {
            return false;
        }
        if (!AllFinite(rot0) || !AllFinite(rot1) ||
            !AllFinite(rot2) || !AllFinite(rot3)) {
            if (error) *error = "Gaussian PLY contains a non-finite or out-of-range value.";
            return false;
        }
        result.rotations.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            bool identity = false;
            bool changed = false;
            if (!NormalizeQuaternion(
                    {rot0[row], rot1[row], rot2[row], rot3[row]},
                    &result.rotations[row], &identity, &changed)) {
                if (error) *error = "Gaussian PLY contains an invalid quaternion.";
                return false;
            }
            if (identity) {
                ++identityQuaternions;
            } else if (changed) {
                ++normalizedQuaternions;
            }
        }
    }

    {
        std::vector<float> opacity;
        if (!takeColumn("opacity", &opacity)) {
            return false;
        }
        if (!AllFinite(opacity)) {
            if (error) *error = "Gaussian PLY contains a non-finite or out-of-range value.";
            return false;
        }
        result.opacities.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            result.opacities[row] = Sigmoid(opacity[row]);
        }
    }

    {
        std::vector<float> dc0;
        std::vector<float> dc1;
        std::vector<float> dc2;
        if (!takeColumn("f_dc_0", &dc0) ||
            !takeColumn("f_dc_1", &dc1) ||
            !takeColumn("f_dc_2", &dc2)) {
            return false;
        }
        if (!AllFinite(dc0) || !AllFinite(dc1) || !AllFinite(dc2)) {
            if (error) *error = "Gaussian PLY contains a non-finite or out-of-range value.";
            return false;
        }
        result.dcCoefficients.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            result.dcCoefficients[row] = {dc0[row], dc1[row], dc2[row]};
        }
    }

    result.restCoefficients.resize(count * restPerChannel);
    for (std::size_t coefficient = 0;
         coefficient < restPerChannel;
         ++coefficient) {
        std::vector<float> red;
        std::vector<float> green;
        std::vector<float> blue;
        if (!takeColumn(indexedRest.at(coefficient), &red) ||
            !takeColumn(indexedRest.at(restPerChannel + coefficient), &green) ||
            !takeColumn(indexedRest.at(2 * restPerChannel + coefficient), &blue)) {
            return false;
        }
        if (!AllFinite(red) || !AllFinite(green) || !AllFinite(blue)) {
            if (error) *error = "Gaussian PLY contains an invalid SH coefficient.";
            return false;
        }
        for (std::size_t row = 0; row < count; ++row) {
            result.restCoefficients[row * restPerChannel + coefficient] =
                {red[row], green[row], blue[row]};
        }
    }

    std::set<std::string> known(RequiredProperties.begin(), RequiredProperties.end());
    known.insert("nx");
    known.insert("ny");
    known.insert("nz");
    for (const auto& entry : indexedRest) {
        known.insert(entry.second);
    }
    std::size_t unknownProperties = 0;
    for (const PlyPropertyDescription& property : header.vertexProperties) {
        if (known.find(property.name) == known.end()) {
            ++unknownProperties;
        }
    }

    std::string validationError;
    if (!ValidateGaussianCloud(result, &validationError)) {
        if (error) *error = validationError;
        return false;
    }

    AddCountWarning(warnings, normalizedQuaternions,
        "quaternion was normalized.", "quaternions were normalized.");
    AddCountWarning(warnings, identityQuaternions,
        "zero-length quaternion was replaced with identity.",
        "zero-length quaternions were replaced with identity.");
    AddCountWarning(warnings, unknownProperties,
        "unrecognized vertex property was ignored.",
        "unrecognized vertex properties were ignored.");

    *cloud = std::move(result);
    return true;
}

} // namespace openstrata::gs::ply
