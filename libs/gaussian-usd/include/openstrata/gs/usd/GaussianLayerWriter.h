// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianCloudData.h"

#include "pxr/usd/sdf/layer.h"

#include <cstddef>
#include <string>

namespace openstrata::gs::usd {

// The authoring failures the writer can report. Diagnostic codes stay owned by
// the calling format bundle (GSPLY-**** for PLY, GSPZ-**** for SPZ) rather
// than by this shared library: the writer is one implementation, but a user
// sees the code of the format they actually imported, and codes released by a
// bundle are never renumbered or reused. Every member must be a non-null
// string literal with static storage duration.
struct LayerWriterDiagnosticCodes {
    // Null output parameter — internal pipeline misuse, not file content.
    const char* internalError = nullptr;
    // The cloud failed shared validation before any authoring happened.
    const char* cloudValidationFailed = nullptr;
    // The in-memory stage could not be created.
    const char* stageCreationFailed = nullptr;
    // /Asset or /Asset/Splat could not be defined.
    const char* scaffoldAuthoringFailed = nullptr;
    // An attribute value could not be authored.
    const char* attributeAuthoringFailed = nullptr;
    // The extent computation overflowed to a non-finite bound.
    const char* extentOverflow = nullptr;
};

// Authors the format-independent cloud into OpenUSD's standard
// ParticleField3DGaussianSplat schema on an anonymous layer. The cloud is
// consumed: each array is released once it has been copied into its authored
// attribute so the cloud and the authored arrays never coexist in full.
//
// All format bundles author through this one class, so the stage hierarchy,
// schema, metadata policy, stage metrics, and default-prim behavior are
// identical across formats by construction rather than by convention.
class GaussianLayerWriter {
public:
    explicit GaussianLayerWriter(const LayerWriterDiagnosticCodes& codes)
        : _codes(codes)
    {
    }

    bool WriteToLayer(
        GaussianCloudData&& cloud,
        const std::string& sourceFormat,
        PXR_NS::SdfLayerRefPtr* outLayer,
        std::string* error = nullptr) const;

    // Metadata-only authoring (design policy §12.3): the same /Asset and
    // /Asset/Splat structure, stage metrics, custom data, and SH degree, with
    // no per-Gaussian arrays and no extent.
    bool WriteMetadataToLayer(
        std::size_t gaussianCount,
        int shDegree,
        const std::string& sourceFormat,
        PXR_NS::SdfLayerRefPtr* outLayer,
        std::string* error = nullptr) const;

private:
    LayerWriterDiagnosticCodes _codes;
};

} // namespace openstrata::gs::usd
