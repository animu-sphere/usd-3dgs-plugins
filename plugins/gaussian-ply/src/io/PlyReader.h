// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace openstrata::gs::ply {

enum class PlyScalarType {
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Float32,
    Float64,
    Invalid,
};

struct PlyPropertyDescription {
    std::string name;
    PlyScalarType type = PlyScalarType::Invalid;
    bool isList = false;
};

struct PlyHeader {
    std::size_t vertexCount = 0;
    bool binary = false;
    std::vector<PlyPropertyDescription> vertexProperties;
};

struct PlyDocument {
    PlyHeader header;
    std::map<std::string, std::vector<float>> vertexProperties;
};

// Isolates tinyPLY from the semantic decoder. The adapter exposes only scalar
// vertex properties converted to float so the rest of the importer does not
// depend on tinyPLY's buffers or type enum. Values a float cannot represent
// become +/-infinity so the decoder's finiteness validation still rejects
// out-of-range source data.
class PlyReader {
public:
    bool ReadHeader(
        const std::string& path,
        PlyHeader* header,
        std::string* error = nullptr) const;

    bool Read(
        const std::string& path,
        const std::vector<std::string>& requestedProperties,
        PlyDocument* document,
        std::string* error = nullptr) const;
};

} // namespace openstrata::gs::ply
