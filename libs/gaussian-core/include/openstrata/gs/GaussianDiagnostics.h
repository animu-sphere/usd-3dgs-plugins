// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace openstrata::gs {

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSPLY-E003] ...". Each
// format bundle owns its own code namespace (GSPLY-****, GSPZ-****); this
// helper only fixes how a code and a message are joined, so the spelling
// cannot drift between bundles.
// Used when a caller passes a null code. Appending a null `const char*` to a
// std::string is undefined behavior, and a diagnostic path is exactly where a
// crash is least affordable: it runs only when something has already gone
// wrong. Degrading to a visible placeholder keeps the real message reachable
// and makes the missing code obvious in a bug report.
inline constexpr const char* kUnspecifiedDiagnosticCode = "GS-E000";

inline std::string FormatDiagnostic(const char* code, const std::string& message)
{
    if (!code) {
        code = kUnspecifiedDiagnosticCode;
    }
    // Brackets, space, and a code of the usual "GSPLY-E101" width; a longer
    // code only costs one reallocation.
    std::string result;
    result.reserve(message.size() + 16);
    result += '[';
    result += code;
    result += "] ";
    result += message;
    return result;
}

} // namespace openstrata::gs
