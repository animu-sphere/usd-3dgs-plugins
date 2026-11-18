// SPDX-License-Identifier: Apache-2.0
#include "GaussianSplatFileFormat.h"

#include "io/GaussianSplatDecoder.h"
#include "io/GaussianSplatDiagnostics.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/usd/GaussianLayerWriter.h"

#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/sdf/layer.h"

#include <chrono>
#include <future>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GaussianSplatFileFormatTokens,
                        GAUSSIANSPLAT_FILE_FORMAT_TOKENS);

namespace {

// The source-format string authored into customData.gs. The decoder header
// owns the constant so the authored stage and the import statistics agree by
// construction.
constexpr const char* kSourceFormat = openstrata::gs::splat::kSourceFormatToken;

} // namespace

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GaussianSplatFileFormat, SdfFileFormat);
}

// The shared import-statistics seam's output channel for this bundle
// (GaussianImportStats.h): `TF_DEBUG=GSSPLAT_IMPORT_STATS` prints one stable
// line per full import. Collection is skipped entirely when disabled.
TF_DEBUG_CODES(GSSPLAT_IMPORT_STATS);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        GSSPLAT_IMPORT_STATS,
        "gaussian-splat: one line of per-import statistics through the shared "
        "GaussianImportStats seam");
}

GaussianSplatFileFormat::GaussianSplatFileFormat()
    : SdfFileFormat(GaussianSplatFileFormatTokens->Id,
                    GaussianSplatFileFormatTokens->Version,
                    GaussianSplatFileFormatTokens->Target,
                    GaussianSplatFileFormatTokens->Extension)
{}

GaussianSplatFileFormat::~GaussianSplatFileFormat() = default;

bool GaussianSplatFileFormat::CanRead(const std::string& file) const
{
    // A headerless format can only be routed by extension and size
    // (SPLAT_FORMAT.md §4). Signatures of renamed files are checked in Read(),
    // so the user is told what the file is instead of "no plugin found".
    if (SdfFileFormat::GetFileExtension(file) !=
        GaussianSplatFileFormatTokens->Extension.GetString()) {
        return false;
    }
    return openstrata::gs::splat::GaussianSplatDecoder().CanRead(file);
}

bool GaussianSplatFileFormat::Read(SdfLayer* layer,
                                   const std::string& resolvedPath,
                                   bool metadataOnly) const
{
    namespace gssplat = openstrata::gs::splat;

    gssplat::GaussianSplatDecoder decoder;
    std::string error;

    // The writer's failures are reported under this bundle's own stable
    // GSSPLAT-E1xx codes. The struct is six same-typed pointers, so it is
    // assigned by name: a positional list would let a swapped pair compile
    // silently and emit the wrong code to users (see GaussianLayerWriter.h).
    static const openstrata::gs::usd::LayerWriterDiagnosticCodes kWriterCodes =
        [] {
            openstrata::gs::usd::LayerWriterDiagnosticCodes codes;
            codes.internalError = gssplat::diag::kInternalError;
            codes.cloudValidationFailed = gssplat::diag::kCloudValidationFailed;
            codes.stageCreationFailed = gssplat::diag::kStageCreationFailed;
            codes.scaffoldAuthoringFailed =
                gssplat::diag::kScaffoldAuthoringFailed;
            codes.attributeAuthoringFailed =
                gssplat::diag::kAttributeAuthoringFailed;
            codes.extentOverflow = gssplat::diag::kExtentOverflow;
            return codes;
        }();
    const openstrata::gs::usd::GaussianLayerWriter writer(kWriterCodes);

    // Sdf reload executes under an outer SdfChangeBlock. Authoring a detached
    // layer on a worker thread avoids observing that block, then the authored
    // layer is moved into the caller's layer in one transfer without a USDA
    // serialization and reparse round-trip.
    SdfLayerRefPtr generated;

    if (metadataOnly) {
        // Size-only path (design policy §12.3): the count comes from the file
        // size, the SH degree is always 0, and no record is read.
        gssplat::GaussianSplatMetadata metadata;
        if (!decoder.DecodeMetadata(resolvedPath, &metadata, &error)) {
            TF_RUNTIME_ERROR("gaussian-splat: failed to read '%s': %s",
                             resolvedPath.c_str(), error.c_str());
            return false;
        }
        auto task = std::async(std::launch::async, [&]() {
            return writer.WriteMetadataToLayer(metadata.gaussianCount,
                                               metadata.shDegree, kSourceFormat,
                                               &generated, &error);
        });
        if (!task.get()) {
            TF_RUNTIME_ERROR(
                "gaussian-splat: failed to author USD for '%s': %s",
                resolvedPath.c_str(), error.c_str());
            return false;
        }
        layer->TransferContent(generated);
        return true;
    }

    // Statistics are collected only when the debug flag asks for them, so
    // the default import path pays nothing for the seam.
    openstrata::gs::GaussianImportStats stats;
    openstrata::gs::GaussianImportStats* statsOut =
        TfDebug::IsEnabled(GSSPLAT_IMPORT_STATS) ? &stats : nullptr;

    openstrata::gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    if (!decoder.Decode(resolvedPath, &cloud, &warnings, &error, statsOut)) {
        TF_RUNTIME_ERROR("gaussian-splat: failed to read '%s': %s",
                         resolvedPath.c_str(), error.c_str());
        return false;
    }
    for (const std::string& warning : warnings) {
        TF_WARN("gaussian-splat: %s", warning.c_str());
    }

    if (statsOut) {
        stats.warningCount = warnings.size();
        stats.hasBounds = openstrata::gs::ComputeCloudExtent(
            cloud.positions.data(), cloud.scales.data(), cloud.gaussianCount,
            &stats.boundsMinimum, &stats.boundsMaximum);
    }

    // The writer consumes the cloud so its arrays are released as they are
    // authored.
    const auto authorStart = std::chrono::steady_clock::now();
    auto task = std::async(std::launch::async, [&]() {
        return writer.WriteToLayer(std::move(cloud), kSourceFormat, &generated,
                                   &error);
    });
    if (!task.get()) {
        TF_RUNTIME_ERROR("gaussian-splat: failed to author USD for '%s': %s",
                         resolvedPath.c_str(), error.c_str());
        return false;
    }
    if (statsOut) {
        stats.authorSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          authorStart)
                .count();
        TF_DEBUG(GSSPLAT_IMPORT_STATS)
            .Msg("gaussian-splat: %s\n",
                 openstrata::gs::FormatImportStats(stats).c_str());
    }

    layer->TransferContent(generated);
    return true;
}

bool GaussianSplatFileFormat::WriteToFile(const SdfLayer& layer,
                                          const std::string& filePath,
                                          const std::string& comment,
                                          const FileFormatArguments& args) const
{
    (void)layer;
    (void)filePath;
    (void)comment;
    (void)args;
    TF_RUNTIME_ERROR("%s",
                     openstrata::gs::splat::diag::Format(
                         openstrata::gs::splat::diag::kWriteUnsupported,
                         "gaussian-splat is read-only; USD to .splat writing "
                         "is unsupported")
                         .c_str());
    return false;
}

bool GaussianSplatFileFormat::WriteToString(const SdfLayer& layer,
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
