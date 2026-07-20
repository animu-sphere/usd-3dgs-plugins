// SPDX-License-Identifier: Apache-2.0
#include "GaussianSpzFileFormat.h"

#include "io/SpzReader.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/sdf/layer.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GaussianSpzFileFormatTokens, GAUSSIANSPZ_FILE_FORMAT_TOKENS);

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GaussianSpzFileFormat, SdfFileFormat);
}

GaussianSpzFileFormat::GaussianSpzFileFormat()
    : SdfFileFormat(
          GaussianSpzFileFormatTokens->Id,
          GaussianSpzFileFormatTokens->Version,
          GaussianSpzFileFormatTokens->Target,
          GaussianSpzFileFormatTokens->Extension)
{
}

GaussianSpzFileFormat::~GaussianSpzFileFormat() = default;

bool
GaussianSpzFileFormat::CanRead(const std::string& file) const
{
    if (SdfFileFormat::GetFileExtension(file) != "spz") {
        return false;
    }
    return openstrata::gs::spz::SpzReader().CanRead(file);
}

bool
GaussianSpzFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    (void)layer;
    namespace gsspz = openstrata::gs::spz;

    // The container reader is complete; the semantic decoder and the
    // GaussianLayerWriter routing are the next v0.3.0 workstreams
    // (docs/roadmap/current.md). Running the reader first means a defective
    // file already fails with its final stable GSPZ-**** diagnostic, and only
    // a structurally valid container reaches the temporary message below.
    // This development-snapshot state never ships: the release gate requires
    // supported SPZ files to open.
    gsspz::SpzReader reader;
    std::string error;

    if (metadataOnly) {
        gsspz::SpzHeader header;
        if (!reader.ReadHeader(resolvedPath, &header, &error)) {
            TF_RUNTIME_ERROR(
                "gaussian-spz: failed to read '%s': %s",
                resolvedPath.c_str(), error.c_str());
            return false;
        }
    } else {
        gsspz::SpzPackedDocument document;
        if (!reader.Read(resolvedPath, &document, &error)) {
            TF_RUNTIME_ERROR(
                "gaussian-spz: failed to read '%s': %s",
                resolvedPath.c_str(), error.c_str());
            return false;
        }
    }

    TF_RUNTIME_ERROR(
        "gaussian-spz: '%s' is a valid SPZ container, but semantic decoding "
        "is not implemented yet in this v0.3.0 development snapshot.",
        resolvedPath.c_str());
    return false;
}

bool
GaussianSpzFileFormat::WriteToString(
    const SdfLayer& layer,
    std::string* str,
    const std::string& comment) const
{
    SdfFileFormatConstPtr usda = SdfFileFormat::FindByExtension("usda");
    if (usda) {
        return usda->WriteToString(layer, str, comment);
    }
    return layer.ExportToString(str);
}

PXR_NAMESPACE_CLOSE_SCOPE
