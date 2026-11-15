// SPDX-License-Identifier: Apache-2.0
#include "io/PlyReader.h"

#include "openstrata/gs/GaussianFileIO.h"
#include "openstrata/gs/GaussianImportLimits.h"
#include "openstrata/gs/GaussianSizeMath.h"
#include "tinyply.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace openstrata::gs::ply {
namespace {

PlyScalarType ConvertType(tinyply::Type type) noexcept
{
    switch (type) {
    case tinyply::Type::INT8:
        return PlyScalarType::Int8;
    case tinyply::Type::UINT8:
        return PlyScalarType::UInt8;
    case tinyply::Type::INT16:
        return PlyScalarType::Int16;
    case tinyply::Type::UINT16:
        return PlyScalarType::UInt16;
    case tinyply::Type::INT32:
        return PlyScalarType::Int32;
    case tinyply::Type::UINT32:
        return PlyScalarType::UInt32;
    case tinyply::Type::FLOAT32:
        return PlyScalarType::Float32;
    case tinyply::Type::FLOAT64:
        return PlyScalarType::Float64;
    default:
        return PlyScalarType::Invalid;
    }
}

bool BuildHeader(const std::vector<tinyply::PlyElement>& elements, bool binary,
                 PlyHeader* header, std::string* error)
{
    if (!header) {
        if (error)
            *error = "PLY reader received a null header output.";
        return false;
    }

    const tinyply::PlyElement* vertex = nullptr;
    for (const tinyply::PlyElement& element : elements) {
        if (element.name == "vertex") {
            if (vertex) {
                if (error)
                    *error = "PLY header contains duplicate vertex elements.";
                return false;
            }
            vertex = &element;
        }
    }
    if (!vertex) {
        if (error)
            *error = "PLY header does not contain a vertex element.";
        return false;
    }

    PlyHeader result;
    result.vertexCount = vertex->size;
    result.binary = binary;
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

// A single vertex element with only known scalar types has a fixed payload
// size, including fields such as normals that the decoder does not request.
std::optional<std::size_t>
FixedVertexPayloadSize(const std::vector<tinyply::PlyElement>& elements,
                       const PlyHeader& header)
{
    if (!header.binary || elements.size() != 1) {
        return std::nullopt;
    }

    std::size_t rowBytes = 0;
    for (const tinyply::PlyProperty& property : elements.front().properties) {
        const int stride =
            tinyply::PropertyTable.at(property.propertyType).stride;
        if (property.isList || stride == 0) {
            return std::nullopt;
        }
        if (!CheckedAddSize(rowBytes, static_cast<std::size_t>(stride),
                            &rowBytes)) {
            throw std::length_error("PLY vertex row size overflows.");
        }
    }

    std::size_t payloadBytes = 0;
    if (!CheckedMulSize(rowBytes, header.vertexCount, &payloadBytes)) {
        throw std::length_error("PLY vertex payload size overflows.");
    }
    return payloadBytes;
}

bool HasPayloadBytes(std::ifstream& stream, std::size_t payloadBytes)
{
    const auto begin = stream.tellg();
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    stream.seekg(begin);
    return stream && begin >= 0 && end >= begin &&
           static_cast<std::uintmax_t>(end - begin) >= payloadBytes;
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

template <class T> std::vector<float> CopyValues(tinyply::PlyData& data)
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

bool CopyValues(tinyply::PlyData& data, std::vector<float>* values,
                std::string* error)
{
    if (!values || data.isList) {
        if (error)
            *error = "Requested PLY property is not a scalar array.";
        return false;
    }
    switch (data.t) {
    case tinyply::Type::INT8:
        *values = CopyValues<std::int8_t>(data);
        return true;
    case tinyply::Type::UINT8:
        *values = CopyValues<std::uint8_t>(data);
        return true;
    case tinyply::Type::INT16:
        *values = CopyValues<std::int16_t>(data);
        return true;
    case tinyply::Type::UINT16:
        *values = CopyValues<std::uint16_t>(data);
        return true;
    case tinyply::Type::INT32:
        *values = CopyValues<std::int32_t>(data);
        return true;
    case tinyply::Type::UINT32:
        *values = CopyValues<std::uint32_t>(data);
        return true;
    case tinyply::Type::FLOAT32:
        *values = CopyValues<float>(data);
        return true;
    case tinyply::Type::FLOAT64:
        *values = CopyValues<double>(data);
        return true;
    default:
        if (error)
            *error = "Requested PLY property has an unsupported type.";
        return false;
    }
}

std::string ExceptionMessage(const char* context,
                             const std::exception& exception)
{
    std::ostringstream message;
    message << context << ": " << exception.what();
    return message.str();
}

} // namespace

bool PlyReader::ReadHeader(const std::string& path, PlyHeader* header,
                           std::string* error) const
{
    std::ifstream stream =
        openstrata::gs::OpenInputFile(path, std::ios::binary);
    if (!stream) {
        if (error)
            *error = "Could not open PLY file '" + path + "'.";
        return false;
    }
    try {
        tinyply::PlyFile file;
        if (!file.parse_header(stream)) {
            if (error)
                *error = "Could not parse the PLY header.";
            return false;
        }
        return BuildHeader(file.get_elements(), file.is_binary_file(), header,
                           error);
    } catch (const std::exception& exception) {
        if (error)
            *error =
                ExceptionMessage("Could not parse the PLY header", exception);
        return false;
    }
}

bool PlyReader::Read(const std::string& path,
                     const std::vector<std::string>& requestedProperties,
                     PlyDocument* document, std::string* error) const
{
    if (!document) {
        if (error)
            *error = "PLY reader received a null document output.";
        return false;
    }

    std::ifstream stream =
        openstrata::gs::OpenInputFile(path, std::ios::binary);
    if (!stream) {
        if (error)
            *error = "Could not open PLY file '" + path + "'.";
        return false;
    }
    try {
        tinyply::PlyFile file;
        if (!file.parse_header(stream)) {
            if (error)
                *error = "Could not parse the PLY header.";
            return false;
        }

        PlyDocument result;
        const auto elements = file.get_elements();
        if (!BuildHeader(elements, file.is_binary_file(), &result.header,
                         error)) {
            return false;
        }

        // Check the header of the stream that will actually be read before
        // handing its counts to tinyPLY's allocator.
        if (!IsGaussianCountWithinLimit(result.header.vertexCount)) {
            if (error)
                *error = "PLY vertex count exceeds the shared import limit.";
            return false;
        }

        const auto payloadSize =
            FixedVertexPayloadSize(elements, result.header);
        if (payloadSize && !HasPayloadBytes(stream, *payloadSize)) {
            if (error)
                *error = "PLY payload is truncated or unreadable.";
            return false;
        }

        // In tinyPLY 2.3.4, a nonzero list hint enables upfront buffer
        // sizing. Scalar allocation already uses a multiplier of one.
        const std::uint32_t listSizeHint = payloadSize ? 1 : 0;
        std::map<std::string, std::shared_ptr<tinyply::PlyData>> requested;
        for (const std::string& name : requestedProperties) {
            requested.emplace(name, file.request_properties_from_element(
                                        "vertex", {name}, listSizeHint));
        }
        file.read(stream);
        if (stream.fail() || stream.bad()) {
            if (error)
                *error = "PLY payload is truncated or unreadable.";
            return false;
        }

        for (const auto& entry : requested) {
            const std::shared_ptr<tinyply::PlyData>& data = entry.second;
            if (!data) {
                if (error)
                    *error = "tinyPLY returned no data for property '" +
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
        if (error)
            *error =
                ExceptionMessage("Could not read the PLY payload", exception);
        return false;
    }
}

} // namespace openstrata::gs::ply
