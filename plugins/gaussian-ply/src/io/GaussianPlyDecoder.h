// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"

#include <cstddef>
#include <string>
#include <vector>

namespace openstrata::gs::ply {

// The one source-format token: passed to GaussianLayerWriter (authored as
// `gs:sourceFormat`) and reported in import statistics, so the stage and the
// instrumentation cannot disagree.
inline constexpr const char* kSourceFormatToken = "Gaussian Splatting PLY";

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

    // On success, `stats` (optional) carries the decoder's half of the shared
    // import-statistics record: source format/version, count and degree,
    // byte sizes, and the read/decode timings. Bounds and the authoring time
    // stay with the caller.
    bool Decode(
        const std::string& path,
        GaussianCloudData* cloud,
        std::vector<std::string>* warnings = nullptr,
        std::string* error = nullptr,
        GaussianImportStats* stats = nullptr) const;
};

} // namespace openstrata::gs::ply
