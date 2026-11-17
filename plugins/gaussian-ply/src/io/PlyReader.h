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

enum class PlyEncoding {
    Ascii,
    BinaryLittleEndian,
    BinaryBigEndian,
};

struct PlyElementDescription {
    std::string name;
    std::size_t count = 0;
    std::vector<PlyPropertyDescription> properties;
};

struct PlyHeader {
    std::size_t vertexCount = 0;
    bool binary = false;
    PlyEncoding encoding = PlyEncoding::Ascii;
    std::vector<PlyPropertyDescription> vertexProperties;
    // Every element in declaration order, the vertex element included.
    // Property names are reported as declared; only the vertex element is
    // checked for duplicates here.
    std::vector<PlyElementDescription> elements;
    // Header `comment` lines with the keyword and one separator removed.
    std::vector<std::string> comments;
};

// One element's requested scalar properties, row-interleaved in request
// order and in native little-endian bytes. Every property in a request has
// the same scalar type, so a row is `properties.size()` values of that type.
struct PlyElementRows {
    std::string element;
    std::vector<std::string> properties;
    PlyScalarType type = PlyScalarType::Invalid;
    std::size_t rowCount = 0;
    std::size_t rowStride = 0;
    std::vector<unsigned char> bytes;
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
    bool ReadHeader(const std::string& path, PlyHeader* header,
                    std::string* error = nullptr) const;

    bool Read(const std::string& path,
              const std::vector<std::string>& requestedProperties,
              PlyDocument* document, std::string* error = nullptr) const;

    // Raw-row read for layouts whose values must not pass through float
    // (bit-packed integer properties). Each entry of `rows` names an element
    // and its properties in file order on input and receives the rows on
    // output. Only binary little-endian files are accepted, so the bytes are
    // already in host order on every supported platform. `header` receives
    // the header of the stream actually read.
    bool ReadElementRows(const std::string& path,
                         std::vector<PlyElementRows>* rows, PlyHeader* header,
                         std::string* error = nullptr) const;
};

} // namespace openstrata::gs::ply
