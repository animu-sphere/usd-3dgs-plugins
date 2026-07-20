// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/fileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

// The tokens that identify this file format to USD's Sdf layer registry.
#define GAUSSIANSPZ_FILE_FORMAT_TOKENS \
    ((Id, "spz"))         \
    ((Version, "1.0"))              \
    ((Target, "usd"))              \
    ((Extension, "spz"))

TF_DECLARE_PUBLIC_TOKENS(GaussianSpzFileFormatTokens, GAUSSIANSPZ_FILE_FORMAT_TOKENS);

/// Read-only SdfFileFormat for Niantic SPZ Gaussian Splatting assets.
/// Container reading lives in io/SpzReader.*; semantic decoding into
/// GaussianCloudData and authoring through the shared GaussianLayerWriter
/// are the remaining v0.3.0 workstreams, so Read() currently reports the
/// container verdict and then fails explicitly.
class GaussianSpzFileFormat : public SdfFileFormat {
public:
    bool CanRead(const std::string& file) const override;
    bool Read(SdfLayer* layer, const std::string& resolvedPath, bool metadataOnly) const override;
    bool WriteToString(
        const SdfLayer& layer,
        std::string* str,
        const std::string& comment = std::string()) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    GaussianSpzFileFormat();
    ~GaussianSpzFileFormat() override;
};

PXR_NAMESPACE_CLOSE_SCOPE
