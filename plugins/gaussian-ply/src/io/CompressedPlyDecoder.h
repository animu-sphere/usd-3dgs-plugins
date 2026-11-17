// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io/PlyReader.h"
#include "openstrata/gs/GaussianCloudData.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

// The SuperSplat compressed PLY dialect of the gaussian-ply bundle. The layout
// and its decode are specified in docs/reference/COMPRESSED_PLY_MAPPING.md;
// GaussianPlyDecoder routes a file here when its header carries the
// compressed signature and keeps the Graphdeco path unchanged otherwise.
namespace openstrata::gs::ply::compressed {

// Vertices per chunk: vertex i uses chunk floor(i / kChunkSize).
inline constexpr std::size_t kChunkSize = 256;
inline constexpr std::size_t kChunkPropertyCount = 18;
inline constexpr std::size_t kVertexPropertyCount = 4;

// Header facts the decode needs, resolved once by ValidateLayout. Column
// indices map canonical property order to the file's declaration order, so
// the decode never depends on the order a producer declares properties in.
struct CompressedLayout {
    std::size_t gaussianCount = 0;
    std::size_t chunkCount = 0;
    int shDegree = 0;
    // Rest coefficients per channel (3, 8, or 15), 0 without an sh element.
    std::size_t restPerChannel = 0;

    std::vector<std::string> chunkProperties;  // file order
    std::vector<std::string> vertexProperties; // file order
    std::vector<std::string> shProperties;     // file order
    std::array<std::size_t, kChunkPropertyCount> chunkColumn{};
    std::array<std::size_t, kVertexPropertyCount> vertexColumn{};
    std::vector<std::size_t> shColumn; // by f_rest index

    // The non-default splat model the header comments name ("mip" or
    // "2dgs"), or empty for the default model.
    std::string renderMode;
};

// Structural, header-only signature (DESIGN_POLICY.md §7.6): a vertex element
// that declares `packed_position` beside a `chunk` element. Disjoint from the
// Graphdeco signature, so the two dialects never claim the same file.
bool HasCompressedSignature(const PlyHeader& header) noexcept;

// Validates everything decidable from the header: encoding, element and
// property sets and types, row counts, SH degree, and the import limit.
// Errors carry their GSPLY code.
bool ValidateLayout(const PlyHeader& header, CompressedLayout* layout,
                    std::string* error);

// Reads and decodes the body into a model-frame (RUB) cloud. `layout` must
// come from ValidateLayout on the same file; the header of the stream
// actually read is validated again before its rows are trusted.
// `readSeconds` (optional) receives the container read time.
bool Decode(const std::string& path, const CompressedLayout& layout,
            GaussianCloudData* cloud, std::vector<std::string>* warnings,
            std::string* error, double* readSeconds = nullptr);

} // namespace openstrata::gs::ply::compressed
