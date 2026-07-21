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
// USD authoring, E2xx file-format entry point, W0xx warnings. The v0.5.0
// decoder fills the E0xx range; the skeleton claims only the codes below.
namespace openstrata::gs::sog::diag {

// The whole format, until the v0.5.0 decoder lands: this release recognizes
// .sog assets but does not decode them. Deliberately E001 in the
// entry-point-style position so a v0.5.0 user of an old package gets one
// unambiguous "upgrade" signal, never a bogus malformed-container error.
inline constexpr const char* kNotImplemented = "GSSOG-E001";

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSSOG-E001] ...". The
// joining itself lives in gaussianCore so it cannot drift between bundles.
inline std::string Format(const char* code, const std::string& message)
{
    return openstrata::gs::FormatDiagnostic(code, message);
}

} // namespace openstrata::gs::sog::diag
