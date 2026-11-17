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
#include <map>
#include <memory>
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

bool BuildHeader(tinyply::PlyFile& file, PlyHeader* header, std::string* error)
{
    const std::vector<tinyply::PlyElement> elements = file.get_elements();
    const bool binary = file.is_binary_file();
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
    result.encoding = !binary              ? PlyEncoding::Ascii :
                      file.is_big_endian() ? PlyEncoding::BinaryBigEndian :
                                             PlyEncoding::BinaryLittleEndian;
    result.comments = file.get_comments();
    result.elements.reserve(elements.size());
    for (const tinyply::PlyElement& element : elements) {
        PlyElementDescription description;
        description.name = element.name;
        description.count = element.size;
        description.properties.reserve(element.properties.size());
        for (const tinyply::PlyProperty& property : element.properties) {
            description.properties.push_back({
                property.name,
                ConvertType(property.propertyType),
                property.isList,
            });
        }
        result.elements.push_back(std::move(description));
    }
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

// Reject impossible counts before tinyPLY allocates the scalar columns.
// Each ASCII token needs at least one byte. For a binary list, count only
// its length field here; tinyPLY checks the contents while reading.
std::size_t MinimumPayloadSize(const std::vector<tinyply::PlyElement>& elements,
                               bool binary)
{
    std::size_t payloadBytes = 0;
    for (const auto& element : elements) {
        std::size_t rowBytes = 0;
        for (const auto& property : element.properties) {
            const auto type =
                property.isList ? property.listType : property.propertyType;
            const std::size_t stride =
                binary ? tinyply::PropertyTable.at(type).stride : 1;
            if (!CheckedAddSize(rowBytes, stride, &rowBytes)) {
                throw std::length_error("PLY element row size overflows.");
            }
        }
        std::size_t elementBytes = 0;
        if (!CheckedMulSize(rowBytes, element.size, &elementBytes) ||
            !CheckedAddSize(payloadBytes, elementBytes, &payloadBytes)) {
            throw std::length_error("PLY payload size overflows.");
        }
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
        return BuildHeader(file, header, error);
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
        if (!BuildHeader(file, &result.header, error)) {
            return false;
        }

        // Check the header of the stream that will actually be read before
        // handing its counts to tinyPLY's allocator.
        if (!IsGaussianCountWithinLimit(result.header.vertexCount)) {
            if (error)
                *error = "PLY vertex count exceeds the shared import limit.";
            return false;
        }

        const auto minimumSize =
            MinimumPayloadSize(elements, result.header.binary);
        if (!HasPayloadBytes(stream, minimumSize)) {
            if (error)
                *error = "PLY payload is truncated or unreadable.";
            return false;
        }

        std::map<std::string, std::shared_ptr<tinyply::PlyData>> requested;
        for (const std::string& name : requestedProperties) {
            auto data = file.request_properties_from_element("vertex", {name});
            if (!data) {
                if (error)
                    *error =
                        "tinyPLY returned no data for property '" + name + "'.";
                return false;
            }
            if (data->isList) {
                if (error)
                    *error = "Requested PLY property is not a scalar array.";
                return false;
            }
            requested.emplace(name, std::move(data));
        }
        file.read(stream);
        if (stream.fail()) {
            if (error)
                *error = "PLY payload is truncated or unreadable.";
            return false;
        }

        for (const auto& entry : requested) {
            const std::shared_ptr<tinyply::PlyData>& data = entry.second;
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

bool PlyReader::ReadElementRows(const std::string& path,
                                std::vector<PlyElementRows>* rows,
                                PlyHeader* header, std::string* error) const
{
    if (!rows || !header) {
        if (error)
            *error = "PLY reader received a null row or header output.";
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

        PlyHeader parsed;
        if (!BuildHeader(file, &parsed, error)) {
            return false;
        }
        if (parsed.encoding != PlyEncoding::BinaryLittleEndian) {
            if (error)
                *error = "PLY raw-row reads require binary_little_endian.";
            return false;
        }
        if (!IsGaussianCountWithinLimit(parsed.vertexCount)) {
            if (error)
                *error = "PLY vertex count exceeds the shared import limit.";
            return false;
        }

        // Every element's rows, requested or not, must be present before
        // tinyPLY allocates the requested ones.
        const auto elements = file.get_elements();
        if (!HasPayloadBytes(stream, MinimumPayloadSize(elements, true))) {
            if (error)
                *error = "PLY payload is truncated or unreadable.";
            return false;
        }

        std::vector<std::shared_ptr<tinyply::PlyData>> requested;
        requested.reserve(rows->size());
        for (PlyElementRows& request : *rows) {
            const PlyElementDescription* element = nullptr;
            for (const PlyElementDescription& candidate : parsed.elements) {
                if (candidate.name == request.element) {
                    element = &candidate;
                    break;
                }
            }
            if (!element || request.properties.empty()) {
                if (error)
                    *error = "PLY element '" + request.element +
                             "' is missing or has no requested properties.";
                return false;
            }
            // tinyPLY sizes a grouped request by one property's type, so a
            // mixed-type group would be read with the wrong stride.
            PlyScalarType type = PlyScalarType::Invalid;
            for (const std::string& name : request.properties) {
                const PlyPropertyDescription* property = nullptr;
                for (const PlyPropertyDescription& candidate :
                     element->properties) {
                    if (candidate.name == name) {
                        property = &candidate;
                        break;
                    }
                }
                if (!property || property->isList ||
                    property->type == PlyScalarType::Invalid ||
                    (type != PlyScalarType::Invalid &&
                     property->type != type)) {
                    if (error)
                        *error = "PLY property '" + request.element + "." +
                                 name +
                                 "' is missing, not scalar, or differs in "
                                 "type from its group.";
                    return false;
                }
                type = property->type;
            }
            auto data = file.request_properties_from_element(
                request.element, request.properties);
            if (!data || data->isList) {
                if (error)
                    *error = "tinyPLY returned no scalar data for element '" +
                             request.element + "'.";
                return false;
            }
            request.type = type;
            request.rowCount = element->count;
            requested.push_back(std::move(data));
        }

        file.read(stream);
        if (stream.fail()) {
            if (error)
                *error = "PLY payload is truncated or unreadable.";
            return false;
        }

        for (std::size_t i = 0; i < rows->size(); ++i) {
            PlyElementRows& request = (*rows)[i];
            tinyply::PlyData& data = *requested[i];
            const std::size_t valueStride =
                tinyply::PropertyTable.at(data.t).stride;
            std::size_t rowStride = 0;
            std::size_t expectedBytes = 0;
            if (!CheckedMulSize(valueStride, request.properties.size(),
                                &rowStride) ||
                !CheckedMulSize(rowStride, request.rowCount, &expectedBytes) ||
                data.buffer.size_bytes() != expectedBytes) {
                if (error)
                    *error = "PLY element '" + request.element +
                             "' row data does not match its declared size.";
                return false;
            }
            request.rowStride = rowStride;
            request.bytes.assign(data.buffer.get_const(),
                                 data.buffer.get_const() + expectedBytes);
            // Release the parser-owned copy before the next element's rows.
            data.buffer = tinyply::Buffer();
        }

        *header = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        if (error)
            *error =
                ExceptionMessage("Could not read the PLY payload", exception);
        return false;
    }
}

} // namespace openstrata::gs::ply
