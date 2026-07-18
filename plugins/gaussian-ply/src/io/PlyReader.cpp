// SPDX-License-Identifier: Apache-2.0
#include "io/PlyReader.h"

#include "tinyply.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>

namespace openstrata::gs::ply {
namespace {

PlyScalarType ConvertType(tinyply::Type type) noexcept
{
    switch (type) {
    case tinyply::Type::INT8: return PlyScalarType::Int8;
    case tinyply::Type::UINT8: return PlyScalarType::UInt8;
    case tinyply::Type::INT16: return PlyScalarType::Int16;
    case tinyply::Type::UINT16: return PlyScalarType::UInt16;
    case tinyply::Type::INT32: return PlyScalarType::Int32;
    case tinyply::Type::UINT32: return PlyScalarType::UInt32;
    case tinyply::Type::FLOAT32: return PlyScalarType::Float32;
    case tinyply::Type::FLOAT64: return PlyScalarType::Float64;
    default: return PlyScalarType::Invalid;
    }
}

bool BuildHeader(
    const tinyply::PlyFile& file,
    PlyHeader* header,
    std::string* error)
{
    if (!header) {
        if (error) *error = "PLY reader received a null header output.";
        return false;
    }

    const std::vector<tinyply::PlyElement> elements = file.get_elements();
    const tinyply::PlyElement* vertex = nullptr;
    for (const tinyply::PlyElement& element : elements) {
        if (element.name == "vertex") {
            if (vertex) {
                if (error) *error = "PLY header contains duplicate vertex elements.";
                return false;
            }
            vertex = &element;
        }
    }
    if (!vertex) {
        if (error) *error = "PLY header does not contain a vertex element.";
        return false;
    }

    PlyHeader result;
    result.vertexCount = vertex->size;
    result.binary = file.is_binary_file();
    std::set<std::string> names;
    result.vertexProperties.reserve(vertex->properties.size());
    for (const tinyply::PlyProperty& property : vertex->properties) {
        if (!names.insert(property.name).second) {
            if (error) {
                *error = "PLY vertex element contains duplicate property '" +
                    property.name + "'.";
            }
            return false;
        }
        result.vertexProperties.push_back({
            property.name,
            ConvertType(property.propertyType),
            property.isList,
        });
    }
    *header = std::move(result);
    return true;
}

// Narrowing a double outside float range is undefined behavior, so map such
// values to +/-infinity explicitly; the decoder rejects non-finite values.
float NarrowToFloat(double value) noexcept
{
    if (value > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::numeric_limits<float>::infinity();
    }
    if (value < -static_cast<double>(std::numeric_limits<float>::max())) {
        return -std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(value);
}

template <class T>
std::vector<float> CopyValues(tinyply::PlyData& data)
{
    std::vector<float> result(data.count);
    const std::uint8_t* bytes = data.buffer.get_const();
    if constexpr (std::is_same_v<T, float>) {
        std::memcpy(result.data(), bytes, data.count * sizeof(float));
    } else {
        for (std::size_t i = 0; i < data.count; ++i) {
            T value{};
            std::memcpy(&value, bytes + i * sizeof(T), sizeof(T));
            if constexpr (std::is_same_v<T, double>) {
                result[i] = NarrowToFloat(value);
            } else {
                result[i] = static_cast<float>(value);
            }
        }
    }
    return result;
}

bool CopyValues(
    tinyply::PlyData& data,
    std::vector<float>* values,
    std::string* error)
{
    if (!values || data.isList) {
        if (error) *error = "Requested PLY property is not a scalar array.";
        return false;
    }
    switch (data.t) {
    case tinyply::Type::INT8: *values = CopyValues<std::int8_t>(data); return true;
    case tinyply::Type::UINT8: *values = CopyValues<std::uint8_t>(data); return true;
    case tinyply::Type::INT16: *values = CopyValues<std::int16_t>(data); return true;
    case tinyply::Type::UINT16: *values = CopyValues<std::uint16_t>(data); return true;
    case tinyply::Type::INT32: *values = CopyValues<std::int32_t>(data); return true;
    case tinyply::Type::UINT32: *values = CopyValues<std::uint32_t>(data); return true;
    case tinyply::Type::FLOAT32: *values = CopyValues<float>(data); return true;
    case tinyply::Type::FLOAT64: *values = CopyValues<double>(data); return true;
    default:
        if (error) *error = "Requested PLY property has an unsupported type.";
        return false;
    }
}

std::string ExceptionMessage(const char* context, const std::exception& exception)
{
    std::ostringstream message;
    message << context << ": " << exception.what();
    return message.str();
}

} // namespace

bool PlyReader::ReadHeader(
    const std::string& path,
    PlyHeader* header,
    std::string* error) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        if (error) *error = "Could not open PLY file '" + path + "'.";
        return false;
    }
    try {
        tinyply::PlyFile file;
        if (!file.parse_header(stream)) {
            if (error) *error = "Could not parse the PLY header.";
            return false;
        }
        return BuildHeader(file, header, error);
    } catch (const std::exception& exception) {
        if (error) *error = ExceptionMessage("Could not parse the PLY header", exception);
        return false;
    }
}

bool PlyReader::Read(
    const std::string& path,
    const std::vector<std::string>& requestedProperties,
    PlyDocument* document,
    std::string* error) const
{
    if (!document) {
        if (error) *error = "PLY reader received a null document output.";
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        if (error) *error = "Could not open PLY file '" + path + "'.";
        return false;
    }
    try {
        tinyply::PlyFile file;
        if (!file.parse_header(stream)) {
            if (error) *error = "Could not parse the PLY header.";
            return false;
        }

        PlyDocument result;
        if (!BuildHeader(file, &result.header, error)) {
            return false;
        }

        std::map<std::string, std::shared_ptr<tinyply::PlyData>> requested;
        for (const std::string& name : requestedProperties) {
            requested.emplace(
                name,
                file.request_properties_from_element("vertex", {name}));
        }
        file.read(stream);
        if (stream.fail() || stream.bad()) {
            if (error) *error = "PLY payload is truncated or unreadable.";
            return false;
        }

        for (const auto& entry : requested) {
            const std::shared_ptr<tinyply::PlyData>& data = entry.second;
            if (!data) {
                if (error) *error = "tinyPLY returned no data for property '" +
                    entry.first + "'.";
                return false;
            }
            std::vector<float> values;
            if (!CopyValues(*data, &values, error)) {
                return false;
            }
            // Drop the parser-owned native buffer as soon as the property is
            // converted so both representations never coexist for the whole
            // payload.
            data->buffer = tinyply::Buffer();
            if (values.size() != result.header.vertexCount) {
                if (error) {
                    *error = "PLY property '" + entry.first +
                        "' count does not match the vertex count.";
                }
                return false;
            }
            result.vertexProperties.emplace(entry.first, std::move(values));
        }

        *document = std::move(result);
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = ExceptionMessage("Could not read the PLY payload", exception);
        return false;
    }
}

} // namespace openstrata::gs::ply
