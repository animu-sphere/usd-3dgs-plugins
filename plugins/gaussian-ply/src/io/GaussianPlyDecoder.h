// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"

#include <cstddef>
#include <string>
#include <vector>

namespace openstrata::gs::ply {

// Header-derivable facts about a Gaussian PLY, available without decoding
// vertex data (design policy §12.3).
struct GaussianPlyMetadata {
    std::size_t gaussianCount = 0;
    int shDegree = 0;
};

class GaussianPlyDecoder {
public:
    bool CanRead(const std::string& path) const noexcept;

    // Validates the header layout and derives count and SH degree without
    // reading vertex data.
    bool DecodeMetadata(
        const std::string& path,
        GaussianPlyMetadata* metadata,
        std::string* error = nullptr) const;

    bool Decode(
        const std::string& path,
        GaussianCloudData* cloud,
        std::vector<std::string>* warnings = nullptr,
        std::string* error = nullptr) const;
};

} // namespace openstrata::gs::ply
