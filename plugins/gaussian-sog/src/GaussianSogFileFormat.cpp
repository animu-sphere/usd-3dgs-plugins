// SPDX-License-Identifier: Apache-2.0
#include "GaussianSogFileFormat.h"

#include "io/GaussianSogDecoder.h"
#include "io/GaussianSogDiagnostics.h"
#include "io/SogReader.h"
#include "openstrata/gs/GaussianCloudData.h"
#include "openstrata/gs/GaussianImportStats.h"
#include "openstrata/gs/GaussianMath.h"
#include "openstrata/gs/usd/GaussianLayerWriter.h"

#include "pxr/base/tf/debug.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/type.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layer.h"

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GaussianSogFileFormatTokens, GAUSSIANSOG_FILE_FORMAT_TOKENS);

namespace {

namespace gssog = openstrata::gs::sog;

// The source-format string authored into customData.gs. The decoder header
// owns the constant so the authored stage and the import statistics agree by
// construction.
constexpr const char* kSourceFormat = gssog::kSourceFormatToken;

// The unbundled layout's property planes are named *relative to* `meta.json`,
// so they are companion assets and are loaded the way USD loads any other
// companion: through the asset resolver, anchored on the layer's own resolved
// path. That is what lets a search-path or packaged resolver find the planes of
// a `meta.json` it resolved itself. The primary asset still arrives as a
// resolved path and is read directly, exactly as in the PLY and SPZ bundles.
bool LoadCompanionThroughResolver(
    const std::string& anchorPath,
    const std::string& planeName,
    std::vector<unsigned char>* bytes,
    std::string* error)
{
    const auto fail = [error](const char* code, const std::string& message) {
        if (error) {
            *error = gssog::diag::Format(code, message);
        }
        return false;
    };

    ArResolver& resolver = ArGetResolver();
    const std::string identifier =
        resolver.CreateIdentifier(planeName, ArResolvedPath(anchorPath));
    const ArResolvedPath resolved =
        resolver.Resolve(identifier.empty() ? planeName : identifier);
    if (!resolved) {
        return fail(gssog::diag::kMissingPlane,
            "The property plane '" + planeName + "' declared by meta.json "
            "could not be resolved relative to '" + anchorPath + "'.");
    }

    const std::shared_ptr<ArAsset> asset = resolver.OpenAsset(resolved);
    if (!asset) {
        return fail(gssog::diag::kUnreadableFile,
            "The property plane '" + planeName + "' resolved to '" +
            resolved.GetPathString() + "' but could not be opened.");
    }
    const std::size_t size = asset->GetSize();
    try {
        bytes->resize(size);
    } catch (const std::exception&) {
        return fail(gssog::diag::kUnreadableFile,
            "A " + std::to_string(size) + "-byte buffer for the property "
            "plane '" + planeName + "' could not be allocated.");
    }
    if (size == 0) {
        return true;
    }
    // A resolver may hand back a mapped buffer or only a stream; both are
    // supported, so a custom resolver need not implement GetBuffer().
    if (const std::shared_ptr<const char> buffer = asset->GetBuffer()) {
        std::copy(buffer.get(), buffer.get() + size, bytes->begin());
        return true;
    }
    if (asset->Read(bytes->data(), size, 0) != size) {
        return fail(gssog::diag::kUnreadableFile,
            "The property plane '" + planeName + "' could not be read from '" +
            resolved.GetPathString() + "'.");
    }
    return true;
}

// One decoder configuration for every read: the reader carries the
// resolver-backed companion loader above.
gssog::GaussianSogDecoder MakeDecoder()
{
    return gssog::GaussianSogDecoder(
        gssog::SogReader(&LoadCompanionThroughResolver));
}

} // namespace

// Register the format with USD's type system so the plug system can find it.
TF_REGISTRY_FUNCTION(TfType)
{
    SDF_DEFINE_FILE_FORMAT(GaussianSogFileFormat, SdfFileFormat);
}

// The shared import-statistics seam's output channel for this bundle
// (GaussianImportStats.h): `TF_DEBUG=GSSOG_IMPORT_STATS` prints one stable
// line per full import. Collection is skipped entirely when disabled.
TF_DEBUG_CODES(GSSOG_IMPORT_STATS);

TF_REGISTRY_FUNCTION(TfDebug)
{
    TF_DEBUG_ENVIRONMENT_SYMBOL(GSSOG_IMPORT_STATS,
        "gaussian-sog: one line of per-import statistics through the shared "
        "GaussianImportStats seam");
}

GaussianSogFileFormat::GaussianSogFileFormat()
    : SdfFileFormat(
          GaussianSogFileFormatTokens->Id,
          GaussianSogFileFormatTokens->Version,
          GaussianSogFileFormatTokens->Target,
          std::vector<std::string>{
              GaussianSogFileFormatTokens->Extension.GetString(),
              GaussianSogFileFormatTokens->MetaExtension.GetString()})
{
}

GaussianSogFileFormat::~GaussianSogFileFormat() = default;

bool
GaussianSogFileFormat::CanRead(const std::string& file) const
{
    // The two layouts have two different gates (SOG_FORMAT.md §6): a bundled
    // `.sog` is claimed by the ZIP signature, while the far broader `.json`
    // registration claims only what actually parses as a SOG v2 `meta.json`.
    // Neither gate validates enough to promise a successful read: a file past
    // it must reach Read() and fail with a specific GSSOG diagnostic rather
    // than be disowned to USD's "no plugin found" (§7.6).
    const std::string extension = SdfFileFormat::GetFileExtension(file);
    const gssog::GaussianSogDecoder decoder;
    if (extension == GaussianSogFileFormatTokens->Extension) {
        return decoder.CanReadBundled(file);
    }
    if (extension == GaussianSogFileFormatTokens->MetaExtension) {
        return decoder.CanReadUnbundled(file);
    }
    return false;
}

bool
GaussianSogFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly) const
{
    const gssog::GaussianSogDecoder decoder = MakeDecoder();
    std::string error;

    // SOG authors through the same GaussianLayerWriter as PLY and SPZ,
    // reporting the writer's failures under this bundle's own stable
    // GSSOG-E1xx codes. The struct is six same-typed pointers, so it is
    // assigned by name: a positional list would let a swapped pair compile
    // silently and emit the wrong code to users (see GaussianLayerWriter.h).
    static const openstrata::gs::usd::LayerWriterDiagnosticCodes kWriterCodes = [] {
        openstrata::gs::usd::LayerWriterDiagnosticCodes codes;
        codes.internalError = gssog::diag::kInternalError;
        codes.cloudValidationFailed = gssog::diag::kCloudValidationFailed;
        codes.stageCreationFailed = gssog::diag::kStageCreationFailed;
        codes.scaffoldAuthoringFailed = gssog::diag::kScaffoldAuthoringFailed;
        codes.attributeAuthoringFailed = gssog::diag::kAttributeAuthoringFailed;
        codes.extentOverflow = gssog::diag::kExtentOverflow;
        return codes;
    }();
    const openstrata::gs::usd::GaussianLayerWriter writer(kWriterCodes);

    // Sdf reload executes under an outer SdfChangeBlock. Authoring a detached
    // layer on a worker thread avoids observing that block, then the authored
    // layer is moved into the caller's layer in one transfer without a USDA
    // serialization and reparse round-trip.
    SdfLayerRefPtr generated;

    if (metadataOnly) {
        // meta.json-only path (design policy §12.3, SOG_MAPPING.md §9): count
        // and SH degree come from the metadata document; not one WebP plane is
        // decoded.
        gssog::GaussianSogMetadata metadata;
        if (!decoder.DecodeMetadata(resolvedPath, &metadata, &error)) {
            TF_RUNTIME_ERROR(
                "gaussian-sog: failed to read '%s': %s",
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
                "gaussian-sog: failed to author USD for '%s': %s",
                resolvedPath.c_str(), error.c_str());
            return false;
        }
        layer->TransferContent(generated);
        return true;
    }

    // Statistics are collected only when the debug flag asks for them, so the
    // default import path pays nothing for the seam.
    openstrata::gs::GaussianImportStats stats;
    openstrata::gs::GaussianImportStats* statsOut =
        TfDebug::IsEnabled(GSSOG_IMPORT_STATS) ? &stats : nullptr;

    openstrata::gs::GaussianCloudData cloud;
    std::vector<std::string> warnings;
    if (!decoder.Decode(resolvedPath, &cloud, &warnings, &error, statsOut)) {
        TF_RUNTIME_ERROR(
            "gaussian-sog: failed to read '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }
    for (const std::string& warning : warnings) {
        TF_WARN("gaussian-sog: %s", warning.c_str());
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
            "gaussian-sog: failed to author USD for '%s': %s",
            resolvedPath.c_str(), error.c_str());
        return false;
    }
    if (statsOut) {
        stats.authorSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - authorStart).count();
        TF_DEBUG(GSSOG_IMPORT_STATS).Msg("gaussian-sog: %s\n",
            openstrata::gs::FormatImportStats(stats).c_str());
    }

    layer->TransferContent(generated);
    return true;
}

bool
GaussianSogFileFormat::WriteToFile(
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
        gssog::diag::Format(
            gssog::diag::kWriteUnsupported,
            "gaussian-sog is read-only; USD to SOG writing is "
            "unsupported").c_str());
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
