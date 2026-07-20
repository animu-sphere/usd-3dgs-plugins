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
// W0xx warnings — mirroring the GSPLY-**** conventions. Only the container
// series exists yet; the semantic decoder extends E0xx when it lands.
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

// Internal misuse of the reader (null output parameters). Reported
// defensively; not reachable from file content.
inline constexpr const char* kInternalError = "GSPZ-E100";

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSPZ-E003] ...". The
// joining itself lives in gaussianCore so it cannot drift between bundles.
inline std::string Format(const char* code, const std::string& message)
{
    return openstrata::gs::FormatDiagnostic(code, message);
}

} // namespace openstrata::gs::spz::diag
