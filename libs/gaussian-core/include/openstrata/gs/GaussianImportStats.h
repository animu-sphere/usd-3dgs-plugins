// SPDX-License-Identifier: Apache-2.0
#pragma once

// The shared per-import statistics record (v0.4.0 import-statistics seam,
// extended for v0.6.0 observability).
//
// Every format bundle instruments an import through this one structure so
// per-format instrumentation cannot diverge: the decoder fills the source and
// timing halves it can see (read, semantic decode), the file-format plugin
// adds the USD-authoring time, and the same formatter renders every format's
// record identically. The record travels *beside* the model, never in it
// (GAUSSIAN_MODEL_CONTRACT.md §3, retained source metadata): nothing here may
// alter how a consumer interprets the model arrays.
//
// The plugin API deliberately does not expose the record; bundles emit it
// through their own debug channel (GSPLY_IMPORT_STATS /
// GSPZ_IMPORT_STATS/GSSOG_IMPORT_STATS TfDebug flags). Inspection tooling
// consumes this same seam rather than growing a second one.

#include "openstrata/gs/GaussianCloudData.h"

#include <cstdint>
#include <string>

namespace openstrata::gs {

enum class GaussianCoordinateConversion {
    None,
    RdfToRub,
};

const char*
CoordinateConversionName(GaussianCoordinateConversion conversion) noexcept;

struct GaussianImportStats {
    // The same source-format token the bundle passes to GaussianLayerWriter
    // (authored as `gs:sourceFormat`), so log lines and stages agree.
    std::string sourceFormat;
    // Format-specific version or encoding label ("binary_little_endian",
    // "ascii", an SPZ container version). Empty when the format has none.
    std::string sourceVersion;

    // The fixed conversion applied by the decoder, not an inferred property
    // of the asset. Failed imports have no statistics record.
    GaussianCoordinateConversion coordinateConversion =
        GaussianCoordinateConversion::None;

    std::size_t gaussianCount = 0;
    int shDegree = 0;

    // Gaussians omitted by a successful user-level import filter. A threshold
    // rejection is included in this total and reported separately below.
    std::size_t rejectedGaussianCount = 0;
    std::size_t opacityThresholdRejectedCount = 0;
    // Number of aggregated warning messages emitted for this import.
    std::size_t warningCount = 0;

    // Container bytes on disk versus the decoded semantic bytes of the model
    // arrays; the pair is what makes compression ratios comparable across
    // formats.
    std::uint64_t sourceBytes = 0;
    std::uint64_t decodedBytes = 0;

    // Model-frame axis-aligned bounds, from ComputeCloudExtent. Optional
    // because computing them costs a pass over the cloud; hasBounds says
    // whether the fields are meaningful.
    bool hasBounds = false;
    Float3 boundsMinimum;
    Float3 boundsMaximum;

    // Wall-clock seconds per pipeline stage: container read, semantic decode
    // (dequantization through validation), and USD authoring. A stage a
    // bundle cannot separate is reported in the later stage, never dropped.
    double readSeconds = 0.0;
    double decodeSeconds = 0.0;
    double authorSeconds = 0.0;
};

// The decoded semantic byte size of a cloud's arrays (positions, scales,
// rotations, opacities, DC and rest coefficients), independent of allocator
// overhead. Uses the declared sizes, so it is meaningful only for a cloud
// that passed validation.
std::uint64_t ComputeDecodedByteSize(const GaussianCloudData& cloud) noexcept;

// One-line, stable, machine-greppable rendering: `key=value` pairs separated
// by single spaces, keys fixed by this function rather than by each bundle.
std::string FormatImportStats(const GaussianImportStats& stats);

} // namespace openstrata::gs
