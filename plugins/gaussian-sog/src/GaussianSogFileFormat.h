// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/fileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

// The tokens that identify this file format to USD's Sdf layer registry.
#define GAUSSIANSOG_FILE_FORMAT_TOKENS \
    ((Id, "sog"))         \
    ((Version, "1.0"))              \
    ((Target, "usd"))              \
    ((Extension, "sog"))

TF_DECLARE_PUBLIC_TOKENS(GaussianSogFileFormatTokens, GAUSSIANSOG_FILE_FORMAT_TOKENS);

/// The v0.4.0 skeleton registration for `.sog`: recognizes the extension and
/// rejects every read with the stable GSSOG-E001 diagnostic. The v0.5.0 SOG
/// v2 decoder replaces the Read body with the real reader/decoder pipeline
/// targeting the shared Gaussian model contract.
class GaussianSogFileFormat : public SdfFileFormat {
public:
    bool CanRead(const std::string& file) const override;
    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;
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
