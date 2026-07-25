// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstrata/gs/GaussianDiagnostics.h"

#include <string>

// Stable diagnostic identifiers for every error and warning this plugin can
// emit. The identifiers are a compatibility surface: external tools may match
// on them, so a code is never renumbered or reused once released. The
// machine-readable catalog shipped with the plugin resources
// (plugin/resources/gaussian-sog/diagnostics.json) must list every code
// defined here; tests/test_gaussian_sog_plugin.py cross-checks the two.
//
// Allocation plan for the namespace, mirroring the GSPLY-****/GSPZ-****
// conventions: E0xx container and semantic decoding, E1xx internal misuse and
// USD authoring, E2xx file-format entry point, W0xx warnings.
//
// GSSOG-E001 (the v0.4.0 skeleton's "SOG import is not implemented") is
// **retired** now that decoding lands: it is absent from this header and from
// the catalog, and is never reused. A user of an old package still sees it
// from that package's own binary, which is exactly the upgrade signal it was
// allocated for.
namespace openstrata::gs::sog::diag {

// Container errors (SogReader). The malformed / unsupported / internal
// distinction is a release criterion: a user handed a legacy SOG v1 file is
// told the version is not supported (E003), not that the file is corrupt.
inline constexpr const char* kNotSogContainer = "GSSOG-E002";
inline constexpr const char* kUnsupportedVersion = "GSSOG-E003";
inline constexpr const char* kMalformedArchive = "GSSOG-E004";
inline constexpr const char* kMalformedMetadata = "GSSOG-E005";
inline constexpr const char* kInvalidGaussianCount = "GSSOG-E006";
inline constexpr const char* kInvalidShBands = "GSSOG-E007";
inline constexpr const char* kMissingPlane = "GSSOG-E008";
inline constexpr const char* kMalformedPlane = "GSSOG-E009";
inline constexpr const char* kUnreadableFile = "GSSOG-E010";
inline constexpr const char* kInvalidCodebook = "GSSOG-E011";
// A well-formed SOG that declares no Gaussians. `meta.count == 0` is valid
// SOG (SOG_MAPPING.md §2) but cannot become a shared model: the contract
// requires N > 0 and forbids authoring an empty stage that misrepresents its
// source (GAUSSIAN_MODEL_CONTRACT.md §3). Reported as its own code, never as
// a malformed container, so the message can say which of the two it is.
inline constexpr const char* kEmptyPointSet = "GSSOG-E012";

// Semantic decoding errors (GaussianSogDecoder). Every plane dequantizes from
// bounded integers, so — unlike PLY/SPZ — no non-finite value can enter from
// pixel data; the one per-Gaussian semantic failure is a corrupt
// largest-component tag in the quaternion plane, which has no valid reading
// (SOG_MAPPING.md §5) and is not silently replaced with identity.
inline constexpr const char* kMalformedRotation = "GSSOG-E013";
inline constexpr const char* kCloudValidationFailed = "GSSOG-E014";
// A count/degree whose derived model size overflows, or whose model
// allocation the platform refuses (GAUSSIAN_MODEL_CONTRACT.md §3, maximum
// count and overflow). The container's own `count <= W*H` plane bound (E006)
// usually fires first; this is the model-side backstop.
inline constexpr const char* kModelAllocationFailed = "GSSOG-E015";

// Internal misuse of the import pipeline (null output parameters,
// inconsistent reader output). Reported defensively; not reachable from file
// content.
inline constexpr const char* kInternalError = "GSSOG-E100";

// USD authoring errors, injected into the shared GaussianLayerWriter.
inline constexpr const char* kStageCreationFailed = "GSSOG-E101";
inline constexpr const char* kScaffoldAuthoringFailed = "GSSOG-E102";
inline constexpr const char* kAttributeAuthoringFailed = "GSSOG-E103";
inline constexpr const char* kExtentOverflow = "GSSOG-E104";

// File-format entry-point errors.
inline constexpr const char* kWriteUnsupported = "GSSOG-E201";

// Warnings.
// A palette label past `meta.shN.count` has no centroid; the reference
// decoder yields zero rest coefficients for that Gaussian, and so does this
// one — but the file is degraded, so it is reported once with a count rather
// than silently accepted or per-Gaussian spammed.
inline constexpr const char* kShLabelsOutOfRange = "GSSOG-W001";

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSSOG-E003] ...". The
// joining itself lives in gaussianCore so it cannot drift between bundles.
inline std::string Format(const char* code, const std::string& message)
{
    return openstrata::gs::FormatDiagnostic(code, message);
}

} // namespace openstrata::gs::sog::diag
