// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "io/SogReader.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"

#include <cstddef>
#include <string>
#include <vector>

namespace openstrata::gs::sog {

// The one source-format token: passed to GaussianLayerWriter (authored as
// `gs:sourceFormat`) and reported in import statistics, so the stage and the
// instrumentation cannot disagree.
inline constexpr const char* kSourceFormatToken = "Gaussian Splatting SOG";

// Facts about a SOG asset available from `meta.json` alone, without decoding a
// property plane (design policy §12.3, SOG_MAPPING.md §9).
struct GaussianSogMetadata {
    std::size_t gaussianCount = 0;
    int shDegree = 0;
};

// Semantic decoding of a SOG v2 container into the format-independent
// GaussianCloudData: split-precision inverse-log positions, exponential scale
// codebook lookup, smallest-three quaternion unpacking, raw-DC and opacity
// decoding, and palette-resolved higher-order SH. SOG stores PLY-native
// (Graphdeco RDF) columns, so the decoder applies the same shared FlipYZAxes
// conversion into the model's RUB frame that the PLY decoder applies
// (ADR 0001, SOG_MAPPING.md §5). Container concerns — layout detection, ZIP
// walking, `meta.json` schema, WebP decoding, plane dimensions — stay in
// SogReader; this class consumes its document. The exact mapping is
// docs/reference/SOG_MAPPING.md.
class GaussianSogDecoder {
public:
    GaussianSogDecoder() = default;
    // The reader used for every read below. The file-format plugin injects one
    // carrying an asset-resolver-backed companion loader; the default reads
    // unbundled planes from `meta.json`'s own directory.
    explicit GaussianSogDecoder(SogReader reader);

    // Routing (SOG_FORMAT.md §6). The two layouts are claimed by different
    // extensions, so the caller says which gate applies rather than having
    // this class guess from a path.
    bool CanReadBundled(const std::string& path) const noexcept;
    bool CanReadUnbundled(const std::string& path) const noexcept;

    // Parses `meta.json` and derives count and SH degree without decoding a
    // property plane. A zero-Gaussian file fails here exactly as it fails in
    // Decode(): a metadata read must not promise a decode that would be
    // rejected.
    bool DecodeMetadata(
        const std::string& path,
        GaussianSogMetadata* metadata,
        std::string* error = nullptr) const;

    // On success, `stats` (optional) carries the decoder's half of the shared
    // import-statistics record: source format/version, count and degree, byte
    // sizes, and the read/decode timings. Bounds and the authoring time stay
    // with the caller.
    bool Decode(
        const std::string& path,
        GaussianCloudData* cloud,
        std::vector<std::string>* warnings = nullptr,
        std::string* error = nullptr,
        GaussianImportStats* stats = nullptr) const;

private:
    SogReader _reader;
};

} // namespace openstrata::gs::sog
