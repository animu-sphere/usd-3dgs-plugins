// SPDX-License-Identifier: Apache-2.0
#include "GaussianSogFileFormat.h"

#include "io/GaussianSogDiagnostics.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/sdf/layer.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GaussianSogFileFormatTokens, GAUSSIANSOG_FILE_FORMAT_TOKENS);

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GaussianSogFileFormat, SdfFileFormat);
}

GaussianSogFileFormat::GaussianSogFileFormat()
    : SdfFileFormat(
          GaussianSogFileFormatTokens->Id,
          GaussianSogFileFormatTokens->Version,
          GaussianSogFileFormatTokens->Target,
          GaussianSogFileFormatTokens->Extension)
{
}

GaussianSogFileFormat::~GaussianSogFileFormat() = default;

bool
GaussianSogFileFormat::CanRead(const std::string& file) const
{
    // Claimed by extension alone so Read() reports the specific GSSOG-E001
    // not-implemented diagnostic instead of USD reporting that no plugin was
    // found (§7.6: CanRead selects the plugin, diagnostics explain the
    // failure). The v0.5.0 decoder replaces this with the real signature
    // check for both SOG layouts (bundled ZIP, unbundled meta.json).
    return SdfFileFormat::GetFileExtension(file) == "sog";
}

bool
GaussianSogFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    (void)layer;
    (void)metadataOnly;

    // The skeleton's entire read behavior. An empty stage would be worse
    // than this error: nothing in this repository authors a stage that
    // misrepresents its source.
    TF_RUNTIME_ERROR(
        "gaussian-sog: failed to read '%s': %s",
        resolvedPath.c_str(),
        openstrata::gs::sog::diag::Format(
            openstrata::gs::sog::diag::kNotImplemented,
            "SOG import is not implemented in this release; SOG v2 "
            "one-object import is planned for v0.5.0.").c_str());
    return false;
}

bool
GaussianSogFileFormat::WriteToString(
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
