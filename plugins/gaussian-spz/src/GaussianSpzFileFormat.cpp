// SPDX-License-Identifier: Apache-2.0
#include "GaussianSpzFileFormat.h"

#include "io/GaussianSpzDecoder.h"
#include "io/GaussianSpzDiagnostics.h"
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

TF_DEFINE_PUBLIC_TOKENS(GaussianSpzFileFormatTokens, GAUSSIANSPZ_FILE_FORMAT_TOKENS);

namespace {

// The source-format string authored into customData.gs. SPZ and PLY carry
// different strings, but they are the only per-format inputs to one shared
// writer; the authored hierarchy, schema, and metadata policy are identical.
// The decoder header owns the constant so the authored stage and the import
// statistics agree by construction.
constexpr const char* kSourceFormat = openstrata::gs::spz::kSourceFormatToken;

} // namespace

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GaussianSpzFileFormat, SdfFileFormat);
}

// The shared import-statistics seam's output channel for this bundle
// (GaussianImportStats.h): `TF_DEBUG=GSPZ_IMPORT_STATS` prints one stable
// line per full import. Collection is skipped entirely when disabled.
TF_DEBUG_CODES(GSPZ_IMPORT_STATS);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(GSPZ_IMPORT_STATS,
        "gaussian-spz: one line of per-import statistics through the shared "
        "GaussianImportStats seam");
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
    return openstrata::gs::spz::GaussianSpzDecoder().CanRead(file);
}

bool
GaussianSpzFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    namespace gsspz = openstrata::gs::spz;

    gsspz::GaussianSpzDecoder decoder;
    std::string error;

    // SPZ authors through the same GaussianLayerWriter as PLY, reporting the
    // writer's failures under this bundle's own stable GSPZ-E1xx codes. The
    // struct is six same-typed pointers, so it is assigned by name: a
    // positional list would let a swapped pair compile silently and emit the
    // wrong code to users (see GaussianLayerWriter.h).
    static const openstrata::gs::usd::LayerWriterDiagnosticCodes kWriterCodes = [] {
        openstrata::gs::usd::LayerWriterDiagnosticCodes codes;
        codes.internalError = gsspz::diag::kInternalError;
        codes.cloudValidationFailed = gsspz::diag::kCloudValidationFailed;
        codes.stageCreationFailed = gsspz::diag::kStageCreationFailed;
        codes.scaffoldAuthoringFailed = gsspz::diag::kScaffoldAuthoringFailed;
        codes.attributeAuthoringFailed = gsspz::diag::kAttributeAuthoringFailed;
        codes.extentOverflow = gsspz::diag::kExtentOverflow;
        return codes;
    }();
    const openstrata::gs::usd::GaussianLayerWriter writer(kWriterCodes);

    // Sdf reload executes under an outer SdfChangeBlock. Authoring a detached
    // layer on a worker thread avoids observing that block, then the authored
    // layer is moved into the caller's layer in one transfer without a USDA
    // serialization and reparse round-trip.
    SdfLayerRefPtr generated;

    if (metadataOnly) {
        // Header-only path (design policy §12.3): count and SH degree come
        // from the container header; no attribute streams are decompressed.
        gsspz::GaussianSpzMetadata metadata;
        if (!decoder.DecodeMetadata(resolvedPath, &metadata, &error)) {
            TF_RUNTIME_ERROR(
                "gaussian-spz: failed to read '%s': %s",
                resolvedPath.c_str(), error.c_str());
            return false;
        }
        auto task = std::async(std::launch::async, [&]() {
            return writer.WriteMetadataToLayer(
                metadata.gaussianCount, metadata.shDegree,
                kSourceFormat, &generated, &error);
        });
        if (!task.get()) {
            TF_RUNTIME_ERROR(
                "gaussian-spz: failed to author USD for '%s': %s",
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
        TfDebug::IsEnabled(GSPZ_IMPORT_STATS) ? &stats : nullptr;

    openstrata::gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    if (!decoder.Decode(resolvedPath, &cloud, &warnings, &error, statsOut)) {
        TF_RUNTIME_ERROR(
            "gaussian-spz: failed to read '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }
    for (const std::string& warning : warnings) {
        TF_WARN("gaussian-spz: %s", warning.c_str());
    }

    if (statsOut) {
        stats.hasBounds = openstrata::gs::ComputeCloudExtent(
            cloud.positions.data(), cloud.scales.data(), cloud.gaussianCount,
            &stats.boundsMinimum, &stats.boundsMaximum);
    }

    // The writer consumes the cloud so its arrays are released as they are
    // authored.
    const auto authorStart = std::chrono::steady_clock::now();
    auto task = std::async(std::launch::async, [&]() {
        return writer.WriteToLayer(
            std::move(cloud), kSourceFormat, &generated, &error);
    });
    if (!task.get()) {
        TF_RUNTIME_ERROR(
            "gaussian-spz: failed to author USD for '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }
    if (statsOut) {
        stats.authorSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - authorStart).count();
        TF_DEBUG(GSPZ_IMPORT_STATS).Msg("gaussian-spz: %s\n",
            openstrata::gs::FormatImportStats(stats).c_str());
    }

    layer->TransferContent(generated);
    return true;
}

bool
GaussianSpzFileFormat::WriteToFile(
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
        openstrata::gs::spz::diag::Format(
            openstrata::gs::spz::diag::kWriteUnsupported,
            "gaussian-spz is read-only; USD to SPZ writing is "
            "unsupported").c_str());
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
