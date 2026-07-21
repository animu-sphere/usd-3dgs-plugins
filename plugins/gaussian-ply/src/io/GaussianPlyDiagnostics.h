// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianDiagnostics.h"

#include <string>

// Stable diagnostic identifiers for every error and warning this plugin can
// emit. The identifiers are a compatibility surface: external tools may match
// on them, so a code is never renumbered or reused once released. The
// machine-readable catalog shipped with the plugin resources
// (plugin/resources/gaussian-ply/diagnostics.json) must list every code
// defined here; tests/test_gaussian_ply_plugin.py cross-checks the two.
namespace openstrata::gs::ply::diag {

// Decoder and PLY container errors.
inline constexpr const char* kNotGaussianDialect = "GSPLY-E001";
inline constexpr const char* kEmptyVertexElement = "GSPLY-E002";
inline constexpr const char* kMissingRequiredProperty = "GSPLY-E003";
inline constexpr const char* kUnsupportedPropertyType = "GSPLY-E004";
inline constexpr const char* kInvalidRestPropertyName = "GSPLY-E005";
inline constexpr const char* kDuplicateRestIndex = "GSPLY-E006";
inline constexpr const char* kNonContiguousRestIndices = "GSPLY-E007";
inline constexpr const char* kRestCountNotRgb = "GSPLY-E008";
inline constexpr const char* kInvalidShDegree = "GSPLY-E009";
inline constexpr const char* kNonFiniteValue = "GSPLY-E010";
inline constexpr const char* kNonFiniteShCoefficient = "GSPLY-E011";
inline constexpr const char* kLogScaleOverflow = "GSPLY-E012";
inline constexpr const char* kInvalidQuaternion = "GSPLY-E013";
inline constexpr const char* kPropertyCountMismatch = "GSPLY-E014";
inline constexpr const char* kUnreadableContainer = "GSPLY-E015";
inline constexpr const char* kCloudValidationFailed = "GSPLY-E016";
inline constexpr const char* kUnsupportedShDegree = "GSPLY-E017";
// A declared count/degree whose derived model size overflows, or whose model
// allocation the platform refuses (GAUSSIAN_MODEL_CONTRACT.md §3, maximum
// count and overflow). A partial cloud is never produced.
inline constexpr const char* kModelAllocationFailed = "GSPLY-E018";

// Internal misuse of the import pipeline (null output parameters). Reported
// defensively; not reachable from file content.
inline constexpr const char* kInternalError = "GSPLY-E100";

// USD authoring errors.
inline constexpr const char* kStageCreationFailed = "GSPLY-E101";
inline constexpr const char* kScaffoldAuthoringFailed = "GSPLY-E102";
inline constexpr const char* kAttributeAuthoringFailed = "GSPLY-E103";
inline constexpr const char* kExtentOverflow = "GSPLY-E104";

// File-format entry-point errors.
inline constexpr const char* kInvalidFormatArgument = "GSPLY-E201";
inline constexpr const char* kAllGaussiansFiltered = "GSPLY-E202";
inline constexpr const char* kWriteUnsupported = "GSPLY-E203";

// Warnings.
inline constexpr const char* kQuaternionsNormalized = "GSPLY-W001";
inline constexpr const char* kQuaternionsReplaced = "GSPLY-W002";
inline constexpr const char* kUnknownPropertiesIgnored = "GSPLY-W003";

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSPLY-E003] ...". The
// joining itself lives in gaussianCore so it cannot drift between bundles.
inline std::string Format(const char* code, const std::string& message)
{
    return openstrata::gs::FormatDiagnostic(code, message);
}

} // namespace openstrata::gs::ply::diag
