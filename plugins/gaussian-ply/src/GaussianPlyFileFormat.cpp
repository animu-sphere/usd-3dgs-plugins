// SPDX-License-Identifier: Apache-2.0
#include "GaussianPlyFileFormat.h"

#include "io/GaussianPlyDecoder.h"
#include "io/GaussianPlyDiagnostics.h"
#include "io/GaussianPlyImportOptions.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "usd/GaussianLayerWriter.h"

#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/sdf/layer.h"

#include <future>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GaussianPlyFileFormatTokens, GAUSSIANPLY_FILE_FORMAT_TOKENS);

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GaussianPlyFileFormat, SdfFileFormat);
}

GaussianPlyFileFormat::GaussianPlyFileFormat()
    : SdfFileFormat(
          GaussianPlyFileFormatTokens->Id,
          GaussianPlyFileFormatTokens->Version,
          GaussianPlyFileFormatTokens->Target,
          GaussianPlyFileFormatTokens->Extension)
{
}

GaussianPlyFileFormat::~GaussianPlyFileFormat() = default;

bool
GaussianPlyFileFormat::CanRead(const std::string& file) const
{
    if (SdfFileFormat::GetFileExtension(file) != "ply") {
        return false;
    }
    return openstrata::gs::ply::GaussianPlyDecoder().CanRead(file);
}

bool
GaussianPlyFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    namespace gsply = openstrata::gs::ply;

    std::string error;
    gsply::GaussianPlyImportOptions options;
    if (!gsply::ParseImportOptions(
            layer->GetFileFormatArguments(), &options, &error)) {
        TF_RUNTIME_ERROR(
            "gaussian-ply: failed to read '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }

    gsply::GaussianPlyDecoder decoder;
    openstrata::gs::usd::GaussianLayerWriter writer;

    // Sdf reload executes under an outer SdfChangeBlock. Authoring a detached
    // layer on a worker thread avoids observing that block, then the authored
    // layer is moved into the caller's layer in one transfer without a USDA
    // serialization and reparse round-trip.
    SdfLayerRefPtr generated;

    if (metadataOnly) {
        // Header-only path (design policy §12.3): count and SH degree come
        // from the vertex declaration; no vertex data is decoded. The count
        // reported here is the source count — opacityThreshold filtering
        // requires a full read and is not applied to metadata.
        gsply::GaussianPlyMetadata metadata;
        if (!decoder.DecodeMetadata(resolvedPath, &metadata, &error)) {
            TF_RUNTIME_ERROR(
                "gaussian-ply: failed to read '%s': %s",
                resolvedPath.c_str(), error.c_str());
            return false;
        }
        const int effectiveDegree =
            gsply::EffectiveShDegree(options, metadata.shDegree);
        auto task = std::async(std::launch::async, [&]() {
            return writer.WriteMetadataToLayer(
                metadata.gaussianCount, effectiveDegree,
                "Gaussian Splatting PLY", &generated, &error);
        });
        if (!task.get()) {
            TF_RUNTIME_ERROR(
                "gaussian-ply: failed to author USD for '%s': %s",
                resolvedPath.c_str(), error.c_str());
            return false;
        }
        layer->TransferContent(generated);
        return true;
    }

    openstrata::gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    if (!decoder.Decode(resolvedPath, &cloud, &warnings, &error)) {
        TF_RUNTIME_ERROR(
            "gaussian-ply: failed to read '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }
    for (const std::string& warning : warnings) {
        TF_WARN("gaussian-ply: %s", warning.c_str());
    }

    if (!gsply::ApplyImportOptions(options, &cloud, &error)) {
        TF_RUNTIME_ERROR(
            "gaussian-ply: failed to read '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }

    // The writer consumes the cloud so its arrays are released as they are
    // authored.
    auto task = std::async(std::launch::async, [&]() {
        return writer.WriteToLayer(
            std::move(cloud), "Gaussian Splatting PLY", &generated, &error);
    });
    if (!task.get()) {
        TF_RUNTIME_ERROR(
            "gaussian-ply: failed to author USD for '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }

    layer->TransferContent(generated);
    return true;
}

bool
GaussianPlyFileFormat::WriteToFile(
    const SdfLayer& layer,
    const std::string& filePath,
    const std::string& comment,
    const FileFormatArguments& args) const
{
    (void)layer;
    (void)filePath;
    (void)comment;
    (void)args;
    TF_RUNTIME_ERROR("%s",
        openstrata::gs::ply::diag::Format(
            openstrata::gs::ply::diag::kWriteUnsupported,
            "gaussian-ply is read-only; USD to Gaussian PLY writing is "
            "unsupported").c_str());
    return false;
}

bool
GaussianPlyFileFormat::WriteToString(
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
