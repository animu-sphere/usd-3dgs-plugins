// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianDiagnostics.h"

#include <string>

// Stable diagnostic identifiers for every error and warning this plugin can
// emit. The identifiers are a compatibility surface: external tools may match
// on them, so a code is never renumbered or reused once released. The
// machine-readable catalog shipped with the plugin resources
// (plugin/resources/gaussian-spz/diagnostics.json) must list every code
// defined here.
//
// Allocation plan for the namespace: E0xx container and semantic decoding,
// E1xx internal misuse and USD authoring, E2xx file-format entry point,
// W0xx warnings — mirroring the GSPLY-**** conventions.
namespace openstrata::gs::spz::diag {

// Container errors (SpzReader). The malformed / unsupported / internal
// distinction is a v0.3.0 release criterion: a user handed an SPZ v4 file is
// told the version is not supported yet (E003), not that the file is corrupt.
inline constexpr const char* kNotSpzContainer = "GSPZ-E001";
inline constexpr const char* kEmptyPointSet = "GSPZ-E002";
inline constexpr const char* kUnsupportedVersion = "GSPZ-E003";
inline constexpr const char* kMalformedContainer = "GSPZ-E004";
inline constexpr const char* kInvalidPointCount = "GSPZ-E005";
inline constexpr const char* kInvalidShDegree = "GSPZ-E006";
inline constexpr const char* kTruncatedContainer = "GSPZ-E007";
inline constexpr const char* kCorruptContainer = "GSPZ-E008";
inline constexpr const char* kTrailingData = "GSPZ-E009";
inline constexpr const char* kUnreadableFile = "GSPZ-E010";

// Semantic decoding errors (GaussianSpzDecoder). E011 is *unsupported*, not
// malformed: SH degree 4 is valid SPZ, but the shared model carries degrees
// 0-3 (SPZ_FORMAT.md §7), so the file is rejected with an explanation rather
// than silently truncated. E012 is only reachable through v1 float16
// positions; every other attribute dequantizes from bounded integers.
inline constexpr const char* kUnsupportedShDegree = "GSPZ-E011";
inline constexpr const char* kNonFinitePosition = "GSPZ-E012";
inline constexpr const char* kCloudValidationFailed = "GSPZ-E013";
// A count/degree whose derived model size overflows, or whose model
// allocation the platform refuses (GAUSSIAN_MODEL_CONTRACT.md §3, maximum
// count and overflow). The container's own payload plausibility bound
// (E005/E007) usually fires first; this is the model-side backstop.
inline constexpr const char* kModelAllocationFailed = "GSPZ-E014";

// Internal misuse of the import pipeline (null output parameters,
// inconsistent container spans). Reported defensively; not reachable from
// file content.
inline constexpr const char* kInternalError = "GSPZ-E100";

// USD authoring errors, injected into the shared GaussianLayerWriter.
inline constexpr const char* kStageCreationFailed = "GSPZ-E101";
inline constexpr const char* kScaffoldAuthoringFailed = "GSPZ-E102";
inline constexpr const char* kAttributeAuthoringFailed = "GSPZ-E103";
inline constexpr const char* kExtentOverflow = "GSPZ-E104";

// File-format entry-point errors.
inline constexpr const char* kWriteUnsupported = "GSPZ-E201";

// Warnings.
inline constexpr const char* kExtensionsIgnored = "GSPZ-W001";
inline constexpr const char* kAntialiasedFlagIgnored = "GSPZ-W002";

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSPZ-E003] ...". The
// joining itself lives in gaussianCore so it cannot drift between bundles.
inline std::string Format(const char* code, const std::string& message)
{
    return openstrata::gs::FormatDiagnostic(code, message);
}

} // namespace openstrata::gs::spz::diag
