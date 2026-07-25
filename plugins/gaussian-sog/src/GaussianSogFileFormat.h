// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/fileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

// The tokens that identify this file format to USD's Sdf layer registry.
// SOG ships in two layouts (SOG_FORMAT.md §1), so the format claims two
// extensions: `sog` for the bundled ZIP archive and `json` for the unbundled
// `meta.json`. The broad `json` registration is deliberate and maintainer
// ratified — it is the stock unbundled layout's own file name — and is kept
// honest by a strict `CanRead()` gate rather than by the extension.
#define GAUSSIANSOG_FILE_FORMAT_TOKENS \
    ((Id, "sog"))                      \
    ((Version, "1.0"))                 \
    ((Target, "usd"))                  \
    ((Extension, "sog"))               \
    ((MetaExtension, "json"))

TF_DECLARE_PUBLIC_TOKENS(GaussianSogFileFormatTokens, GAUSSIANSOG_FILE_FORMAT_TOKENS);

/// Reads SOG v2 — PlayCanvas "Splat Object Graphics" — into the shared Gaussian
/// model and authors it through the one shared `GaussianLayerWriter`, so the
/// stage a SOG import produces is structurally identical to a PLY or SPZ
/// import. Container work lives in `SogReader`, semantic decoding in
/// `GaussianSogDecoder`; this class is only the `SdfFileFormat` integration.
class GaussianSogFileFormat : public SdfFileFormat {
public:
    bool CanRead(const std::string& file) const override;
    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;
    bool WriteToFile(
        const SdfLayer& layer,
        const std::string& filePath,
        const std::string& comment = std::string(),
        const FileFormatArguments& args = FileFormatArguments()) const override;
    bool WriteToString(
        const SdfLayer& layer,
        std::string* str,
        const std::string& comment = std::string()) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    GaussianSogFileFormat();
    ~GaussianSogFileFormat() override;
};

PXR_NAMESPACE_CLOSE_SCOPE
