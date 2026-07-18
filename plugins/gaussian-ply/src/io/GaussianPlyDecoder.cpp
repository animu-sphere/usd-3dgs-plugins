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

bool ToFloat(double value, float* result)
{
    if (!result || !std::isfinite(value) ||
        value < -std::numeric_limits<float>::max() ||
        value > std::numeric_limits<float>::max()) {
        return false;
    }
    *result = static_cast<float>(value);
    return std::isfinite(*result);
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
    auto values = [&](const std::string& name) -> const std::vector<double>* {
        const auto found = document.vertexProperties.find(name);
        return found == document.vertexProperties.end() ? nullptr : &found->second;
    };
    auto value = [&](const std::string& name, std::size_t row, float* result) {
        const std::vector<double>* array = values(name);
        return array && row < array->size() && ToFloat((*array)[row], result);
    };

    GaussianCloudData result;
    result.gaussianCount = header.vertexCount;
    result.shDegree = shDegree;
    result.positions.reserve(header.vertexCount);
    result.scales.reserve(header.vertexCount);
    result.rotations.reserve(header.vertexCount);
    result.opacities.reserve(header.vertexCount);
    result.dcCoefficients.reserve(header.vertexCount);
    result.restCoefficients.reserve(header.vertexCount * restPerChannel);

    std::size_t normalizedQuaternions = 0;
    std::size_t identityQuaternions = 0;
    for (std::size_t row = 0; row < header.vertexCount; ++row) {
        Float3 position;
        Float3 storedScale;
        Quaternion storedRotation;
        float storedOpacity = 0.0f;
        Float3 dc;
        if (!value("x", row, &position.x) ||
            !value("y", row, &position.y) ||
            !value("z", row, &position.z) ||
            !value("scale_0", row, &storedScale.x) ||
            !value("scale_1", row, &storedScale.y) ||
            !value("scale_2", row, &storedScale.z) ||
            !value("rot_0", row, &storedRotation.real) ||
            !value("rot_1", row, &storedRotation.i) ||
            !value("rot_2", row, &storedRotation.j) ||
            !value("rot_3", row, &storedRotation.k) ||
            !value("opacity", row, &storedOpacity) ||
            !value("f_dc_0", row, &dc.x) ||
            !value("f_dc_1", row, &dc.y) ||
            !value("f_dc_2", row, &dc.z)) {
            if (error) *error = "Gaussian PLY contains a non-finite or out-of-range value.";
            return false;
        }

        Float3 scale;
        if (!DecodeLogScale(storedScale, &scale)) {
            if (error) *error = "Gaussian PLY scale cannot be converted from log space.";
            return false;
        }
        Quaternion rotation;
        bool identity = false;
        bool changed = false;
        if (!NormalizeQuaternion(
                storedRotation, &rotation, &identity, &changed)) {
            if (error) *error = "Gaussian PLY contains an invalid quaternion.";
            return false;
        }
        if (identity) {
            ++identityQuaternions;
        } else if (changed) {
            ++normalizedQuaternions;
        }

        result.positions.push_back(position);
        result.scales.push_back(scale);
        result.rotations.push_back(rotation);
        result.opacities.push_back(Sigmoid(storedOpacity));
        result.dcCoefficients.push_back(dc);

        for (std::size_t coefficient = 0;
             coefficient < restPerChannel;
             ++coefficient) {
            Float3 rgb;
            if (!value(indexedRest.at(coefficient), row, &rgb.x) ||
                !value(indexedRest.at(restPerChannel + coefficient), row, &rgb.y) ||
                !value(indexedRest.at(2 * restPerChannel + coefficient), row, &rgb.z)) {
                if (error) *error = "Gaussian PLY contains an invalid SH coefficient.";
                return false;
            }
            result.restCoefficients.push_back(rgb);
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
