// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianDiagnostics.h"

#include <string>

// Stable diagnostic identifiers for every error and warning this plugin can
// emit. The identifiers are a compatibility surface: external tools may match
// on them, so a code is never renumbered or reused once released. The
// machine-readable catalog shipped with the plugin resources
// (plugin/resources/gaussian-splat/diagnostics.json) must list every code
// defined here.
//
// Allocation plan for the namespace: E0xx file layout and semantic decoding,
// E1xx internal misuse and USD authoring, E2xx file-format entry point,
// W0xx warnings, mirroring the GSPLY-**** and GSPZ-**** conventions.
namespace openstrata::gs::splat::diag {

// File layout errors (SplatReader). `.splat` has no header, so the only
// structure to check is the file size and, for a file renamed from another
// format, that format's signature (SPLAT_FORMAT.md §4).
inline constexpr const char* kEmptyFile = "GSSPLAT-E001";
inline constexpr const char* kPartialRecord = "GSSPLAT-E002";
// The file starts with the signature of a PLY, SPZ v4, SOG, or GLB file. It
// is reported as another format, not as a damaged `.splat` file.
inline constexpr const char* kForeignFormat = "GSSPLAT-E003";
// The record count implied by the file size exceeds the shared import safety
// ceiling (IMPORT_LIMITS.md).
inline constexpr const char* kImportLimitExceeded = "GSSPLAT-E004";
inline constexpr const char* kUnreadableFile = "GSSPLAT-E005";

// Semantic decoding errors (GaussianSplatDecoder). Positions and scales are
// raw float32 values, so they are the only fields that can carry a value the
// model rejects; colour, alpha, and rotation bytes always decode in range.
inline constexpr const char* kNonFinitePosition = "GSSPLAT-E006";
inline constexpr const char* kInvalidScale = "GSSPLAT-E007";
inline constexpr const char* kCloudValidationFailed = "GSSPLAT-E008";
// A record count whose model allocation the platform refuses
// (GAUSSIAN_MODEL_CONTRACT.md §3, maximum count and overflow).
inline constexpr const char* kModelAllocationFailed = "GSSPLAT-E009";

// Internal misuse of the import pipeline (null output parameters,
// inconsistent record buffers). Reported defensively; not reachable from file
// content.
inline constexpr const char* kInternalError = "GSSPLAT-E100";

// USD authoring errors, injected into the shared GaussianLayerWriter.
inline constexpr const char* kStageCreationFailed = "GSSPLAT-E101";
inline constexpr const char* kScaffoldAuthoringFailed = "GSSPLAT-E102";
inline constexpr const char* kAttributeAuthoringFailed = "GSSPLAT-E103";
inline constexpr const char* kExtentOverflow = "GSSPLAT-E104";

// File-format entry-point errors.
inline constexpr const char* kWriteUnsupported = "GSSPLAT-E201";

// Warnings.
// Rotation bytes of 128, 128, 128, 128 decode to a zero quaternion, which the
// shared model replaces with identity (GAUSSIAN_MODEL_CONTRACT.md §3).
inline constexpr const char* kZeroRotationReplaced = "GSSPLAT-W001";

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSSPLAT-E003] ...". The
// joining itself lives in gaussianCore so it cannot drift between bundles.
inline std::string Format(const char* code, const std::string& message)
{
    return openstrata::gs::FormatDiagnostic(code, message);
}

} // namespace openstrata::gs::splat::diag
