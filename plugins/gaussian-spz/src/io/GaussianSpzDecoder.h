// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"

#include <cstddef>
#include <string>
#include <vector>

namespace openstrata::gs::spz {

// The one source-format token: passed to GaussianLayerWriter (authored as
// `gs:sourceFormat`) and reported in import statistics, so the stage and the
// instrumentation cannot disagree.
inline constexpr const char* kSourceFormatToken = "Gaussian Splatting SPZ";

// Header-derivable facts about an SPZ asset, available without decompressing
// the attribute streams (design policy §12.3).
struct GaussianSpzMetadata {
    std::size_t gaussianCount = 0;
    int shDegree = 0;
};

// Semantic decoding of an SPZ v1-v3 container into the format-independent
// GaussianCloudData: position/scale dequantization, rotation decoding and
// normalization, opacity decoding, and SH dequantization. SPZ's native RUB
// frame is the model's reference frame (ADR 0001), so no frame conversion is
// applied. Container concerns (signature, gzip framing, truncation, size
// math) stay in SpzReader; this class consumes its packed document. The
// exact mapping is docs/reference/SPZ_MAPPING.md.
class GaussianSpzDecoder {
public:
    bool CanRead(const std::string& path) const noexcept;

    // Validates the header and derives count and SH degree without touching
    // the attribute streams. SH degree 4 fails here the same way it fails in
    // Decode(): a metadata read must not promise a decode that would be
    // rejected.
    bool DecodeMetadata(
        const std::string& path,
        GaussianSpzMetadata* metadata,
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

} // namespace openstrata::gs::spz
