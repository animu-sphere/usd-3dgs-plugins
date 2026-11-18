// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"

#include <cstddef>
#include <string>
#include <vector>

namespace openstrata::gs::splat {

// The one source-format token: passed to GaussianLayerWriter (authored as
// `gs:sourceFormat`) and reported in import statistics, so the stage and the
// instrumentation cannot disagree.
inline constexpr const char* kSourceFormatToken = "Gaussian Splatting SPLAT";

// What a `.splat` file declares without decoding its records: the count, from
// the file size, and SH degree 0, because the format stores no rest
// coefficients (design policy §12.3).
struct GaussianSplatMetadata {
    std::size_t gaussianCount = 0;
    int shDegree = 0;
};

// Semantic decoding of a `.splat` file into the format-independent
// GaussianCloudData: float32 positions and linear scales, 8-bit colour,
// alpha, and rotation. Every observed writer stores Graphdeco PLY-frame (RDF)
// data, so the decoder applies the shared FlipYZAxes conversion to RUB, as the
// PLY and SOG decoders do (ADR 0001). File layout concerns stay in
// SplatReader. The exact mapping is docs/reference/SPLAT_MAPPING.md.
class GaussianSplatDecoder {
  public:
    bool CanRead(const std::string& path) const noexcept;

    // Validates the file layout and signature and derives the count without
    // reading the records.
    bool DecodeMetadata(const std::string& path,
                        GaussianSplatMetadata* metadata,
                        std::string* error = nullptr) const;

    // On success, `stats` (optional) carries the decoder's half of the shared
    // import-statistics record: source format, count and degree, byte sizes,
    // and the read/decode timings. Bounds and the authoring time stay with
    // the caller.
    bool Decode(const std::string& path, GaussianCloudData* cloud,
                std::vector<std::string>* warnings = nullptr,
                std::string* error = nullptr,
                GaussianImportStats* stats = nullptr) const;
};

} // namespace openstrata::gs::splat
