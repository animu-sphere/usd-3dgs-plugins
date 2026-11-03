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
#include "pxr/usd/ar/packageUtils.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/layer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(GaussianSogFileFormatTokens,
                        GAUSSIANSOG_FILE_FORMAT_TOKENS);

namespace {

namespace gssog = openstrata::gs::sog;

// The source-format string authored into customData.gs. The decoder header
// owns the constant so the authored stage and the import statistics agree by
// construction.
constexpr const char* kSourceFormat = gssog::kSourceFormatToken;

// The primary asset -- the `.sog` archive or `meta.json` -- is read through
// the asset resolver rather than the filesystem: USD hands Read() a resolved
// path, and only `ArResolver::OpenAsset` knows what it names. For a plain file
// that is the file; for a package-relative path (`scene.usdz[meta.json]`) it
// is an entry inside the package; for a custom resolver it may be a URI that
// no filesystem call could open (RESOLVER_MATRIX.md). The reader asks for a
// small prefix first and refuses an oversized asset from GetSize() before the
// bulk read, so the size bounds hold for every resolver.
bool LoadPrimaryThroughResolver(const std::string& resolvedPath,
                                std::size_t limit,
                                std::vector<unsigned char>* bytes,
                                std::uint64_t* size, std::string* error)
{
    const auto fail = [error](const std::string& message) {
        *error = gssog::diag::Format(gssog::diag::kUnreadableFile, message);
        return false;
    };

    const std::shared_ptr<ArAsset> asset =
        ArGetResolver().OpenAsset(ArResolvedPath(resolvedPath));
    if (!asset) {
        return fail("The asset could not be opened for reading: " +
                    resolvedPath);
    }
    *size = static_cast<std::uint64_t>(asset->GetSize());
    const std::size_t want =
        static_cast<std::size_t>(std::min<std::uint64_t>(*size, limit));
    try {
        bytes->resize(want);
    } catch (const std::exception&) {
        return fail("A " + std::to_string(want) + "-byte buffer for '" +
                    resolvedPath + "' could not be allocated.");
    }
    if (want != 0 && asset->Read(bytes->data(), want, 0) != want) {
        return fail("The asset could not be read: " + resolvedPath);
    }
    return true;
}

// The unbundled layout's property planes are named *relative to* `meta.json`,
// so they are companion assets and are loaded the way USD loads any other
// companion: through the asset resolver, anchored on the layer's own resolved
// path. That is what lets a search-path or packaged resolver find the planes of
// a `meta.json` it resolved itself -- including the sibling entries of a
// `meta.json` inside a `.usdz`.
bool LoadCompanionThroughResolver(const std::string& anchorPath,
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

    // The plane is named with an explicit "./" so the identifier is always
    // anchored on meta.json. A bare "scales.webp" is a *search-path* asset
    // path to ArDefaultResolver: when no file of that name sits beside
    // meta.json, CreateIdentifier keeps it unanchored and Resolve then finds
    // whatever "scales.webp" the current directory or the context's search
    // path holds -- silently importing another asset's plane. SogReader has
    // already confined planeName to a bare file name, so the prefix cannot
    // change which directory it names (RESOLVER_MATRIX.md).
    //
    // A package-relative anchor (`scene.usdz[cloud/meta.json]`) is anchored
    // here rather than by CreateIdentifier: Ar anchors a package-relative
    // anchor on its *outer package path*, so "./scales.webp" would name a file
    // beside `scene.usdz`, not the sibling entry inside it -- the same
    // other-asset import the "./" prefix exists to prevent. The innermost
    // packaged path's directory is joined with the bare plane name instead,
    // and package entries always use '/'.
    ArResolver& resolver = ArGetResolver();
    std::string identifier;
    if (ArIsPackageRelativePath(anchorPath)) {
        const std::pair<std::string, std::string> split =
            ArSplitPackageRelativePathInner(anchorPath);
        const std::string::size_type slash = split.second.find_last_of('/');
        const std::string directory = slash == std::string::npos ?
                                          std::string() :
                                          split.second.substr(0, slash + 1);
        identifier =
            ArJoinPackageRelativePath(split.first, directory + planeName);
    } else {
        identifier = resolver.CreateIdentifier("./" + planeName,
                                               ArResolvedPath(anchorPath));
    }
    const ArResolvedPath resolved =
        identifier.empty() ? ArResolvedPath() : resolver.Resolve(identifier);
    if (!resolved) {
        return fail(gssog::diag::kMissingPlane,
                    "The property plane '" + planeName +
                        "' declared by meta.json "
                        "could not be resolved relative to '" +
                        anchorPath + "'.");
    }

    const std::shared_ptr<ArAsset> asset = resolver.OpenAsset(resolved);
    if (!asset) {
        return fail(gssog::diag::kUnreadableFile,
                    "The property plane '" + planeName + "' resolved to '" +
                        resolved.GetPathString() +
                        "' but could not be opened.");
    }
    const std::size_t size = asset->GetSize();
    if (size > gssog::kMaxPlaneBytes) {
        return fail(gssog::diag::kImportLimitExceeded,
                    "The property plane '" + planeName + "' is " +
                        std::to_string(size) + " bytes, above the " +
                        std::to_string(gssog::kMaxPlaneBytes) +
                        "-byte input limit.");
    }
    try {
        bytes->resize(size);
    } catch (const std::exception&) {
        return fail(gssog::diag::kUnreadableFile,
                    "A " + std::to_string(size) +
                        "-byte buffer for the property "
                        "plane '" +
                        planeName + "' could not be allocated.");
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
                    "The property plane '" + planeName +
                        "' could not be read from '" +
                        resolved.GetPathString() + "'.");
    }
    return true;
}

// One decoder configuration for every read and routing decision: the reader
// carries the resolver-backed primary and companion loaders above.
gssog::GaussianSogDecoder MakeDecoder()
{
    return gssog::GaussianSogDecoder(gssog::SogReader(
        &LoadCompanionThroughResolver, &LoadPrimaryThroughResolver));
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
    TF_DEBUG_ENVIRONMENT_SYMBOL(
        GSSOG_IMPORT_STATS,
        "gaussian-sog: one line of per-import statistics through the shared "
        "GaussianImportStats seam");
}

GaussianSogFileFormat::GaussianSogFileFormat()
    : SdfFileFormat(GaussianSogFileFormatTokens->Id,
                    GaussianSogFileFormatTokens->Version,
                    GaussianSogFileFormatTokens->Target,
                    std::vector<std::string>{
                        GaussianSogFileFormatTokens->Extension.GetString(),
                        GaussianSogFileFormatTokens->MetaExtension.GetString()})
{}

GaussianSogFileFormat::~GaussianSogFileFormat() = default;

bool GaussianSogFileFormat::CanRead(const std::string& file) const
{
    // The two layouts have two different gates (SOG_FORMAT.md §6): a bundled
    // `.sog` is claimed by the ZIP signature, while the far broader `.json`
    // registration claims only what actually parses as a SOG v2 `meta.json`.
    // Neither gate validates enough to promise a successful read: a file past
    // it must reach Read() and fail with a specific GSSOG diagnostic rather
    // than be disowned to USD's "no plugin found" (§7.6).
    const std::string extension = SdfFileFormat::GetFileExtension(file);
    const gssog::GaussianSogDecoder decoder = MakeDecoder();
    if (extension == GaussianSogFileFormatTokens->Extension) {
        return decoder.CanReadBundled(file);
    }
    if (extension == GaussianSogFileFormatTokens->MetaExtension) {
        return decoder.CanReadUnbundled(file);
    }
    return false;
}

bool GaussianSogFileFormat::Read(SdfLayer* layer,
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
    static const openstrata::gs::usd::LayerWriterDiagnosticCodes kWriterCodes =
        [] {
            openstrata::gs::usd::LayerWriterDiagnosticCodes codes;
            codes.internalError = gssog::diag::kInternalError;
            codes.cloudValidationFailed = gssog::diag::kCloudValidationFailed;
            codes.stageCreationFailed = gssog::diag::kStageCreationFailed;
            codes.scaffoldAuthoringFailed =
                gssog::diag::kScaffoldAuthoringFailed;
            codes.attributeAuthoringFailed =
                gssog::diag::kAttributeAuthoringFailed;
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
            TF_RUNTIME_ERROR("gaussian-sog: failed to read '%s': %s",
                             resolvedPath.c_str(), error.c_str());
            return false;
        }
        auto task = std::async(std::launch::async, [&]() {
            return writer.WriteMetadataToLayer(metadata.gaussianCount,
                                               metadata.shDegree, kSourceFormat,
                                               &generated, &error);
        });
        if (!task.get()) {
            TF_RUNTIME_ERROR("gaussian-sog: failed to author USD for '%s': %s",
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
        TF_RUNTIME_ERROR("gaussian-sog: failed to read '%s': %s",
                         resolvedPath.c_str(), error.c_str());
        return false;
    }
    for (const std::string& warning : warnings) {
        TF_WARN("gaussian-sog: %s", warning.c_str());
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
        TF_RUNTIME_ERROR("gaussian-sog: failed to author USD for '%s': %s",
                         resolvedPath.c_str(), error.c_str());
        return false;
    }
    if (statsOut) {
        stats.authorSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          authorStart)
                .count();
        TF_DEBUG(GSSOG_IMPORT_STATS)
            .Msg("gaussian-sog: %s\n",
                 openstrata::gs::FormatImportStats(stats).c_str());
    }

    layer->TransferContent(generated);
    return true;
}

bool GaussianSogFileFormat::WriteToFile(const SdfLayer& layer,
                                        const std::string& filePath,
                                        const std::string& comment,
                                        const FileFormatArguments& args) const
{
    (void)layer;
    (void)filePath;
    (void)comment;
    (void)args;
    TF_RUNTIME_ERROR(
        "%s",
        gssog::diag::Format(gssog::diag::kWriteUnsupported,
                            "gaussian-sog is read-only; USD to SOG writing is "
                            "unsupported")
            .c_str());
    return false;
}

bool GaussianSogFileFormat::WriteToString(const SdfLayer& layer,
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
