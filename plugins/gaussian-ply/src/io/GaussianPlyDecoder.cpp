// SPDX-License-Identifier: Apache-2.0
#include "io/GaussianPlyDecoder.h"

#include "io/GaussianPlyDiagnostics.h"
#include "io/PlyReader.h"
#include "openstrata/gs/GaussianMath.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
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

void SetError(std::string* error, const char* code, const std::string& message)
{
    if (error) {
        *error = diag::Format(code, message);
    }
}

// Shared by Decode and DecodeMetadata: everything decidable from the header
// alone — dialect detection, required-property presence and types, the
// f_rest_* index layout, and the SH degree.
bool ValidateHeaderLayout(
    const PlyHeader& header,
    std::map<std::size_t, std::string>* indexedRest,
    int* shDegree,
    std::string* error)
{
    const auto properties = PropertyMap(header);
    const bool hasAnyGaussianProperty =
        properties.find("scale_0") != properties.end() ||
        properties.find("rot_0") != properties.end() ||
        properties.find("opacity") != properties.end() ||
        properties.find("f_dc_0") != properties.end();
    if (!HasGaussianSignature(properties) && !hasAnyGaussianProperty) {
        SetError(error, diag::kNotGaussianDialect,
            "The file is a valid PLY file, but it is not a supported "
            "Gaussian Splatting PLY dialect.");
        return false;
    }
    if (header.vertexCount == 0) {
        SetError(error, diag::kEmptyVertexElement,
            "Gaussian PLY vertex element contains no particles.");
        return false;
    }

    for (const std::string& name : RequiredProperties) {
        const auto found = properties.find(name);
        if (found == properties.end()) {
            SetError(error, diag::kMissingRequiredProperty,
                "Gaussian PLY is missing required property '" + name + "'.");
            return false;
        }
        if (found->second->isList || found->second->type == PlyScalarType::Invalid) {
            SetError(error, diag::kUnsupportedPropertyType,
                "Gaussian PLY property '" + name +
                "' is not a supported scalar type.");
            return false;
        }
    }

    for (const PlyPropertyDescription& property : header.vertexProperties) {
        if (property.name.rfind("f_rest_", 0) != 0) {
            continue;
        }
        std::size_t index = 0;
        if (!ParseRestIndex(property.name, &index)) {
            SetError(error, diag::kInvalidRestPropertyName,
                "Gaussian PLY has invalid SH property '" + property.name + "'.");
            return false;
        }
        if (property.isList || property.type == PlyScalarType::Invalid) {
            SetError(error, diag::kUnsupportedPropertyType,
                "Gaussian PLY SH property '" + property.name +
                "' is not a supported scalar type.");
            return false;
        }
        if (!indexedRest->emplace(index, property.name).second) {
            SetError(error, diag::kDuplicateRestIndex,
                "Gaussian PLY has duplicate SH property index " +
                std::to_string(index) + ".");
            return false;
        }
    }
    for (std::size_t i = 0; i < indexedRest->size(); ++i) {
        if (indexedRest->find(i) == indexedRest->end()) {
            SetError(error, diag::kNonContiguousRestIndices,
                "Gaussian PLY f_rest_* properties are not contiguous.");
            return false;
        }
    }
    if (indexedRest->size() % 3 != 0) {
        SetError(error, diag::kRestCountNotRgb,
            "Gaussian PLY SH property count is not divisible by RGB.");
        return false;
    }

    const std::size_t coefficientsPerGaussian = indexedRest->size() / 3 + 1;
    if (!InferShDegree(coefficientsPerGaussian, shDegree)) {
        SetError(error, diag::kInvalidShDegree,
            "Gaussian PLY SH property count does not form a valid degree.");
        return false;
    }
    // The shared model carries SH degrees 0-kMaxShDegree
    // (GAUSSIAN_MODEL_CONTRACT.md §3). A higher whole degree is well-formed
    // PLY, so the rejection says "not supported" rather than "malformed",
    // and it is never silently truncated to fit.
    if (*shDegree > kMaxShDegree) {
        SetError(error, diag::kUnsupportedShDegree,
            "Gaussian PLY SH degree " + std::to_string(*shDegree) +
            " is not supported; supported degrees are 0-" +
            std::to_string(kMaxShDegree) + ".");
        return false;
    }
    return true;
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
    const char* code,
    std::size_t count,
    const char* singular,
    const char* plural)
{
    if (!warnings || count == 0) {
        return;
    }
    warnings->push_back(diag::Format(code,
        std::to_string(count) + " " + (count == 1 ? singular : plural)));
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

bool GaussianPlyDecoder::DecodeMetadata(
    const std::string& path,
    GaussianPlyMetadata* metadata,
    std::string* error) const
{
    if (!metadata) {
        SetError(error, diag::kInternalError,
            "Gaussian decoder received a null metadata output.");
        return false;
    }

    PlyHeader header;
    std::string readerError;
    if (!PlyReader().ReadHeader(path, &header, &readerError)) {
        SetError(error, diag::kUnreadableContainer, readerError);
        return false;
    }

    std::map<std::size_t, std::string> indexedRest;
    int shDegree = 0;
    if (!ValidateHeaderLayout(header, &indexedRest, &shDegree, error)) {
        return false;
    }

    metadata->gaussianCount = header.vertexCount;
    metadata->shDegree = shDegree;
    return true;
}

bool GaussianPlyDecoder::Decode(
    const std::string& path,
    GaussianCloudData* cloud,
    std::vector<std::string>* warnings,
    std::string* error) const
{
    if (!cloud) {
        SetError(error, diag::kInternalError,
            "Gaussian decoder received a null cloud output.");
        return false;
    }

    PlyReader reader;
    PlyHeader header;
    std::string readerError;
    if (!reader.ReadHeader(path, &header, &readerError)) {
        SetError(error, diag::kUnreadableContainer, readerError);
        return false;
    }

    std::map<std::size_t, std::string> indexedRest;
    int shDegree = 0;
    if (!ValidateHeaderLayout(header, &indexedRest, &shDegree, error)) {
        return false;
    }
    const std::size_t restPerChannel = indexedRest.size() / 3;

    std::vector<std::string> requested = RequiredProperties;
    for (const auto& entry : indexedRest) {
        requested.push_back(entry.second);
    }

    PlyDocument document;
    if (!reader.Read(path, requested, &document, &readerError)) {
        SetError(error, diag::kUnreadableContainer, readerError);
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
            SetError(error, diag::kPropertyCountMismatch,
                "Gaussian PLY property '" + name +
                "' is missing or does not match the vertex count.");
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
            SetError(error, diag::kNonFiniteValue,
                "Gaussian PLY contains a non-finite or out-of-range value.");
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
            SetError(error, diag::kNonFiniteValue,
                "Gaussian PLY contains a non-finite or out-of-range value.");
            return false;
        }
        result.scales.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            if (!DecodeLogScale(
                    {scale0[row], scale1[row], scale2[row]},
                    &result.scales[row])) {
                SetError(error, diag::kLogScaleOverflow,
                    "Gaussian PLY scale cannot be converted from log space.");
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
            SetError(error, diag::kNonFiniteValue,
                "Gaussian PLY contains a non-finite or out-of-range value.");
            return false;
        }
        result.rotations.resize(count);
        for (std::size_t row = 0; row < count; ++row) {
            bool identity = false;
            bool changed = false;
            if (!NormalizeQuaternion(
                    {rot0[row], rot1[row], rot2[row], rot3[row]},
                    &result.rotations[row], &identity, &changed)) {
                SetError(error, diag::kInvalidQuaternion,
                    "Gaussian PLY contains an invalid quaternion.");
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
            SetError(error, diag::kNonFiniteValue,
                "Gaussian PLY contains a non-finite or out-of-range value.");
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
            SetError(error, diag::kNonFiniteValue,
                "Gaussian PLY contains a non-finite or out-of-range value.");
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
            SetError(error, diag::kNonFiniteShCoefficient,
                "Gaussian PLY contains an invalid SH coefficient.");
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

    // Graphdeco PLY conventionally stores right-down-front; the model's
    // reference frame is RUB (GAUSSIAN_MODEL_CONTRACT.md §2, ADR 0001), so
    // decoding negates the Y and Z axes of positions, rotations, and rest SH
    // coefficients. Through v0.3.0 the model frame was PLY-native and the SPZ
    // decoder carried the inverse of this conversion.
    FlipYZAxes(&result);

    std::string validationError;
    if (!ValidateGaussianCloud(result, &validationError)) {
        SetError(error, diag::kCloudValidationFailed, validationError);
        return false;
    }

    AddCountWarning(warnings, diag::kQuaternionsNormalized,
        normalizedQuaternions,
        "quaternion was normalized.", "quaternions were normalized.");
    AddCountWarning(warnings, diag::kQuaternionsReplaced,
        identityQuaternions,
        "zero-length quaternion was replaced with identity.",
        "zero-length quaternions were replaced with identity.");
    AddCountWarning(warnings, diag::kUnknownPropertiesIgnored,
        unknownProperties,
        "unrecognized vertex property was ignored.",
        "unrecognized vertex properties were ignored.");

    *cloud = std::move(result);
    return true;
}

} // namespace openstrata::gs::ply
