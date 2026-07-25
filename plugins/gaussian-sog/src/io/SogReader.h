// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openstrata::gs::sog {

// One decoded 8-bit RGBA property plane. SOG stores every property as a
// lossless WebP image whose texel for Gaussian `i` is at
// `x = i % width, y = i / width` (SOG_MAPPING.md §2), so a plane is addressed
// by Gaussian index rather than by coordinate. Planes are decoded to RGBA even
// when the source has no alpha channel, in which case alpha reads 255.
struct SogPlane {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<unsigned char> rgba; // width * height * 4

    bool Present() const noexcept { return !rgba.empty(); }

    // The four bytes of the texel holding Gaussian `index`. The caller has
    // already established `index < width * height` through the reader's
    // count-versus-plane validation.
    const unsigned char* Gaussian(std::size_t index) const noexcept
    {
        return rgba.data() + index * 4;
    }

    const unsigned char* Texel(std::size_t x, std::size_t y) const noexcept
    {
        return rgba.data() + (y * width + x) * 4;
    }
};

// The `meta.json` facts available without decoding a single property plane —
// the metadata-only read (design policy §12.3, SOG_MAPPING.md §9).
struct SogMetadata {
    int version = 0;
    std::size_t gaussianCount = 0;
    // Higher-order SH bands, 0 when `shN` is absent. SOG v2 admits 1-3, which
    // are exactly SH degrees 1-3, so this doubles as the degree.
    int shBands = 0;
};

// A validated SOG v2 container: the metadata, the dequantization inputs from
// `meta.json`, and every decoded property plane. Everything here is still in
// container terms — codebook indices, split bytes, packed quaternions — and
// becomes model values only in GaussianSogDecoder.
struct SogDocument {
    SogMetadata metadata;

    // Log-domain per-axis position range (SOG_MAPPING.md §4).
    float meansMinimum[3] = {0.0f, 0.0f, 0.0f};
    float meansMaximum[3] = {0.0f, 0.0f, 0.0f};

    // 256-entry codebooks: log scales, raw DC coefficients, and (when `shN` is
    // present) higher-order coefficients.
    std::vector<float> scalesCodebook;
    std::vector<float> sh0Codebook;
    std::vector<float> shCodebook;
    // `meta.shN.count`: how many palette centroids exist. A label at or above
    // it has no centroid and decodes to zero coefficients.
    std::size_t shPaletteCount = 0;

    SogPlane meansLow;
    SogPlane meansHigh;
    SogPlane scales;
    SogPlane quats;
    SogPlane sh0;
    SogPlane shCentroids; // absent when shBands == 0
    SogPlane shLabels;    // absent when shBands == 0

    // Container bytes actually read: the `.sog` archive, or `meta.json` plus
    // every companion plane. Feeds the shared import-statistics record.
    std::uint64_t sourceBytes = 0;
};

// How the unbundled layout loads a property plane named in `meta.json`.
// `anchorPath` is the resolved `meta.json` path and `planeName` the bare file
// name from its `files` array; the loader returns the plane's bytes. The
// default implementation reads `planeName` from `meta.json`'s own directory,
// which is what the unit tests use. The file-format plugin injects an
// asset-resolver-backed loader instead, so a SOG whose planes live behind a
// custom resolver resolves them the same way USD resolves any other
// companion asset — without dragging OpenUSD into this reader.
using SogCompanionLoader = std::function<bool(
    const std::string& anchorPath,
    const std::string& planeName,
    std::vector<unsigned char>* bytes,
    std::string* error)>;

// Owns every SOG v2 container concern: layout detection, ZIP central-directory
// walking (vendored miniz), `meta.json` parsing and schema validation,
// codebook and range checks, lossless-WebP plane decoding (vendored libwebp),
// plane presence and dimension checks, and companion loading for the unbundled
// layout. Errors carry stable GSSOG-E0xx container diagnostics. The reader
// performs no dequantization and constructs no USD objects.
class SogReader {
public:
    SogReader() = default;
    explicit SogReader(SogCompanionLoader companionLoader);

    // Routing decisions (design policy §7.6, SOG_FORMAT.md §6). Neither one
    // validates enough to promise a successful read: a file claimed here and
    // defective past the gate must reach Read() and fail with a specific
    // diagnostic rather than be silently disowned to "no plugin found".

    // A bundled `.sog`: the ZIP local-file signature at offset 0. The archived
    // `meta.json` is deliberately not opened during routing.
    bool CanReadBundled(const std::string& path) const noexcept;

    // An unbundled `meta.json`. The `.json` extension is far too broad to
    // claim on its own, so this gate is strict: the file must parse as a JSON
    // object with `version == 2` and the four required SOG property keys, each
    // carrying a `files` array.
    bool CanReadUnbundled(const std::string& path) const noexcept;

    // Metadata-only path: parses `meta.json` (from the archive for a bundled
    // file) and validates it, decoding no property plane. A valid `meta.json`
    // with a missing or corrupt plane therefore succeeds here and fails in
    // Read() — the same asymmetry the SPZ header path has.
    bool ReadMetadata(
        const std::string& path,
        SogMetadata* metadata,
        std::string* error = nullptr) const;

    bool Read(
        const std::string& path,
        SogDocument* document,
        std::string* error = nullptr) const;

private:
    SogCompanionLoader _companionLoader;
};

} // namespace openstrata::gs::sog
