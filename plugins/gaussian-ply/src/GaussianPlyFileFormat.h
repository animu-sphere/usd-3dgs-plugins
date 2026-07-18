// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/usd/sdf/fileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

// The tokens that identify this file format to USD's Sdf layer registry.
#define GAUSSIANPLY_FILE_FORMAT_TOKENS \
    ((Id, "ply"))         \
    ((Version, "1.0"))              \
    ((Target, "usd"))              \
    ((Extension, "ply"))

TF_DECLARE_PUBLIC_TOKENS(GaussianPlyFileFormatTokens, GAUSSIANPLY_FILE_FORMAT_TOKENS);

/// Read-only SdfFileFormat for Graphdeco-style Gaussian Splat PLY files.
/// Parsed splats are authored as UsdVolParticleField3DGaussianSplat data.
class GaussianPlyFileFormat : public SdfFileFormat {
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

    GaussianPlyFileFormat();
    ~GaussianPlyFileFormat() override;
};

PXR_NAMESPACE_CLOSE_SCOPE
